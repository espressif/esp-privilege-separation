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
#include "riscv/rvruntime-frames.h"
#include "esp_ota_ops.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/rtc.h"
#include "soc/soc.h"
#include "esp_rom_uart.h"
#include "esp_priv_access.h"
#include "esp_private/system_internal.h"

#define INTR_FRAME_SIZE             160      // This value should be same as CONTEXT_SIZE defined in vectors.S

static __attribute__((unused)) const char *TAG = "esp_priv_access_panic";

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

static bool _is_task_wdt_timeout;

#ifdef CONFIG_PA_BACKTRACE_INFO
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

    ((RvExcFrame *)frame)->sp = (uint32_t)(frame) + INTR_FRAME_SIZE;

    uint32_t mcause = ((RvExcFrame *)frame)->mcause;

    esp_priv_access_int_t intr = esp_priv_access_get_int_status();
    ets_printf("\n=================================================\n");
    ets_printf("User app exception occurred:\n");
    if (intr != 0) {
        if (_is_task_wdt_timeout) {
            ets_printf("Guru Meditation Error: Task WDT timeout\n");
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
    panic_print_registers(frame, 0);
    print_stack_dump(frame);

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
        ((RvExcFrame *)frame)->mepc = (uint32_t)crash_user_app;
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
