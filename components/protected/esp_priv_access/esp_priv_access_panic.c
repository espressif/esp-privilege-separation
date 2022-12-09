// Copyright 2020-2022 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//         http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_ota_ops.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/rtc.h"
#include "soc/soc.h"
#include "esp_rom_uart.h"
#include "esp_priv_access.h"
#include "esp_private/system_internal.h"
#if CONFIG_IDF_TARGET_ARCH_RISCV
#include "riscv/rvruntime-frames.h"
#endif
#if CONFIG_IDF_TARGET_ARCH_XTENSA
#include "xtensa/xtensa_context.h"
#include "esp_debug_helpers.h"
#endif

#define RV_INTR_FRAME_SIZE          160             // This value should be same as CONTEXT_SIZE defined in vectors.S
#define XT_INTR_FRAME_SIZE          XT_STK_FRMSZ

static __attribute__((unused)) const char *TAG = "esp_priv_access_panic";

static bool _is_task_wdt_timeout;
static uint32_t _task_wdt_user_epc;

#if CONFIG_IDF_TARGET_ARCH_RISCV
static const char *reason[] = {
    "Instruction address misaligned",
    "Instruction access fault",
    "Illegal instruction",
    "Breakpoint",
    "Load address misaligned",
    "Load access fault",
    "Store address misaligned",
    "Store access fault",
    "Environment call from U-mode",
    "Environment call from S-mode",
    NULL,
    "Environment call from M-mode",
    "Instruction page fault",
    "Load page fault",
    NULL,
    "Store page fault",
};
#elif CONFIG_IDF_TARGET_ARCH_XTENSA
static const char *reason[] = {
    "IllegalInstruction", "Syscall", "InstructionFetchError", "LoadStoreError",
    "Level1Interrupt", "Alloca", "IntegerDivideByZero", "PCValue",
    "Privileged", "LoadStoreAlignment", "res", "res",
    "InstrPDAddrError", "LoadStorePIFDataError", "InstrPIFAddrError", "LoadStorePIFAddrError",
    "InstTLBMiss", "InstTLBMultiHit", "InstFetchPrivilege", "res",
    "InstrFetchProhibited", "res", "res", "res",
    "LoadStoreTLBMiss", "LoadStoreTLBMultihit", "LoadStorePrivilege", "res",
    "LoadProhibited", "StoreProhibited", "res", "res",
    "Cp0Dis", "Cp1Dis", "Cp2Dis", "Cp3Dis",
    "Cp4Dis", "Cp5Dis", "Cp6Dis", "Cp7Dis"
};
#endif

#ifdef CONFIG_PA_BACKTRACE_INFO
#if CONFIG_IDF_TARGET_ARCH_RISCV
static void panic_print_registers(const void *f, int core)
{
    uint32_t *regs = (uint32_t *)f;

    // only print ABI name
    const char *desc[] = {
        "MEPC    ", "RA      ", "SP      ", "GP      ", "TP      ", "T0      ", "T1      ", "T2      ",
        "S0/FP   ", "S1      ", "A0      ", "A1      ", "A2      ", "A3      ", "A4      ", "A5      ",
        "A6      ", "A7      ", "S2      ", "S3      ", "S4      ", "S5      ", "S6      ", "S7      ",
        "S8      ", "S9      ", "S10     ", "S11     ", "T3      ", "T4      ", "T5      ", "T6      ",
        "MSTATUS ", "MTVEC   ", "MCAUSE  ", "MTVAL   ", "MHARTID "
    };

    ets_printf("Core  %d register dump:", ((RvExcFrame *)f)->mhartid);

    for (int x = 0; x < sizeof(desc) / sizeof(desc[0]); x += 4) {
        ets_printf("\r\n");
        for (int y = 0; y < 4 && x + y < sizeof(desc) / sizeof(desc[0]); y++) {
            if (desc[x + y][0] != 0) {
                ets_printf("%s: 0x%08x  ", desc[x + y], regs[x + y]);
            }
        }
    }
}

static void print_stack_dump(void *frame)
{
    ets_printf("\r\n\r\nStack memory:\r\n");
    uint32_t sp = (uint32_t)((RvExcFrame *)frame)->sp;
    const int per_line = 8;
    for (int x = 0; x < 1024; x += per_line * sizeof(uint32_t)) {
        uint32_t *spp = (uint32_t *)(sp + x);
        ets_printf("%08x: ", sp + x);
        for (int y = 0; y < per_line; y++) {
            ets_printf("0x%08x%s", spp[y], y == per_line - 1 ? "\r\n" : " ");
        }
    }
}

#elif CONFIG_IDF_TARGET_ARCH_XTENSA
static void panic_print_registers(const void *f, int core)
{
    XtExcFrame *frame = (XtExcFrame *) f;
    int *regs = (int *)frame;
    (void)regs;

    const char *sdesc[] = {
        "PC      ", "PS      ", "A0      ", "A1      ", "A2      ", "A3      ", "A4      ", "A5      ",
        "A6      ", "A7      ", "A8      ", "A9      ", "A10     ", "A11     ", "A12     ", "A13     ",
        "A14     ", "A15     ", "SAR     ", "EXCCAUSE", "EXCVADDR", "LBEG    ", "LEND    ", "LCOUNT  "
    };

    ets_printf("Core %d register dump:", core);

    for (int x = 0; x < 24; x += 4) {
        ets_printf("\r\n");
        for (int y = 0; y < 4; y++) {
            if (sdesc[x + y][0] != 0) {
                ets_printf("%s: 0x%08x  ", sdesc[x + y], regs[x + y + 1]);
            }
        }
    }
}

static void panic_print_backtrace(const void *f, int core)
{
    XtExcFrame *xt_frame = (XtExcFrame *) f;
    esp_backtrace_frame_t frame = {.pc = xt_frame->pc, .sp = xt_frame->a1, .next_pc = xt_frame->a0, .exc_frame = xt_frame};
    esp_backtrace_print_from_frame(100, &frame, false);
}
#endif
#endif

/* Printing debug information on console consumes substantial amount of time which triggers the
 * interrupt watchdog timer. Increase interrupt wdt timeout from menuconfig
 *
 * Later on, temporarily disable the interrupt watchdog while handling crashed task
 */
IRAM_ATTR void esp_priv_access_handle_crashed_task(void)
{
    StaticTask_t *curr_handle = xTaskGetCurrentTaskHandle();
    /* Interrupt vector stores the exception stack frame
     * at the top of the task TCB
     * Get that exception stack frame from the TCB to print
     * backtrace
     */
    uint32_t *frame = curr_handle->pxDummy1;

#if CONFIG_IDF_TARGET_ARCH_RISCV
    ((RvExcFrame *)frame)->sp = (uint32_t)(frame) + RV_INTR_FRAME_SIZE;

    uint32_t mcause = ((RvExcFrame *)frame)->mcause;
#elif CONFIG_IDF_TARGET_ARCH_XTENSA
    uint32_t mcause = ((XtExcFrame *)frame)->exccause;
#endif

    esp_priv_access_int_t intr = esp_priv_access_get_int_status();
    ets_printf("\n=================================================\n");
    ets_printf("User app exception occurred:\n");
    if (intr != 0) {
        if (_is_task_wdt_timeout) {
            ets_printf("Guru Meditation Error: Task WDT timeout\n");
            /* Replace the PC with the actual PC that caused the task WDT to trigger */
#if CONFIG_IDF_TARGET_ARCH_RISCV
            ((RvExcFrame *)frame)->mepc = (uint32_t)_task_wdt_user_epc;
#elif CONFIG_IDF_TARGET_ARCH_XTENSA
            ((XtExcFrame *)frame)->pc = (uint32_t)_task_wdt_user_epc;
#endif
            _is_task_wdt_timeout = 0;
        } else {
            ets_printf("Guru Meditation Error: Illegal %s access: Fault addr: 0x%x\n", esp_priv_access_int_type_to_str(intr), esp_priv_access_get_fault_addr(intr));
        }
        esp_priv_access_clear_and_reenable_int(intr);
    } else {
        ets_printf("Guru Meditation Error: %s\n", reason[mcause]);
    }
    ets_printf("Troubling task: %s\n", pcTaskGetName(NULL));
    ets_printf("=================================================\n");

#ifdef CONFIG_PA_BACKTRACE_INFO
#if CONFIG_IDF_TARGET_ARCH_RISCV
    panic_print_registers(frame, 0);
    print_stack_dump(frame);
#elif CONFIG_IDF_TARGET_ARCH_XTENSA
    panic_print_registers(frame, 0);
    panic_print_backtrace(frame, 0);
    esp_rom_uart_flush_tx(0);
#endif

    char sha256_buf[65];
    esp_ota_get_app_elf_sha256(sha256_buf, sizeof(sha256_buf));
    ets_printf("\r\n\r\n\r\nELF file SHA256: %s\r\n\r\n", sha256_buf);
    ets_printf("=================================================\n");
#endif

#ifdef CONFIG_PA_DELETE_VIOLATING_TASK
    ets_printf("Deleting %s...\n", pcTaskGetName(curr_handle));
    vTaskDelete(curr_handle);
    ets_printf("=================================================\n");
    portYIELD_FROM_ISR();
#elif CONFIG_PA_RESTART_USER_APP
    esp_priv_access_user_reboot();
    ets_printf("Preparing to restart user_app...\n");
    ets_printf("=================================================\n");
    portYIELD_FROM_ISR();
#elif CONFIG_PA_REBOOT_ENTIRE_SYSTEM
    ets_printf("Rebooting...\n");
    ets_printf("=================================================\n");
    esp_restart_noos_dig();
#endif
}

#if CONFIG_ESP_TASK_WDT && !CONFIG_ESP_TASK_WDT_PANIC
static void crash_user_app()
{
    *(int *)0x0 = 0x0;
}

/*
 *  Task WDT panic handling
 *
 * Define strong esp_task_wdt_isr_user_handler and panic from here
 */
void esp_task_wdt_isr_user_handler(void)
{
    StaticTask_t *handle = xTaskGetCurrentTaskHandle();

    if (pvTaskGetThreadLocalStoragePointer(handle, 1) != NULL) {
#if CONFIG_PA_USER_TASK_WDT_PANIC
        /* This indicates that its a user space task.
         * Change the return address to a crashing function, which results in user space exception
         * and can be handled cleanly without interfering with the protected app
         */
        uint32_t *frame = handle->pxDummy1;
        /* Save the current PC in a variable as this is the PC which caused the task WDT to trigger
         * We will use this PC while generating backtrace
         */
#if CONFIG_IDF_TARGET_ARCH_RISCV
        _task_wdt_user_epc = ((RvExcFrame *)frame)->mepc;
        ((RvExcFrame *)frame)->mepc = (uint32_t)crash_user_app;
#elif CONFIG_IDF_TARGET_ARCH_XTENSA
        _task_wdt_user_epc = ((XtExcFrame *)frame)->pc;
        ((XtExcFrame *)frame)->pc = (uint32_t)crash_user_app;
#endif
        _is_task_wdt_timeout = 1;
#endif
    } else {
#if CONFIG_PA_PROTECTED_TASK_WDT_PANIC
        ESP_EARLY_LOGE(TAG, "Aborting.");
        esp_reset_reason_set_hint(ESP_RST_TASK_WDT);
        abort();
#endif
    }
}
#endif
