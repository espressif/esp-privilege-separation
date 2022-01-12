// Copyright 2020 Espressif Systems (Shanghai) PTE LTD
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

#include <string.h>
#include <esp_image_format.h>
#include <esp_spi_flash.h>
#include <esp_partition.h>
#include "esp_log.h"
#include "esp_intr_alloc.h"
#include "esp_ps.h"
#include "esp_ps_priv.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "riscv/interrupt.h"
#include "soc/world_controller_reg.h"
#include "soc/sensitive_reg.h"
#include "soc/extmem_reg.h"
#include "soc/cache_memory.h"
#include "soc/soc.h"
#include "soc_defs.h"
#include "permc_ll.h"
#include "wcntl_ll.h"
#include "soc/periph_defs.h"
#include "hal/memprot_ll.h"
#include "esp32c3/memprot.h"
#include "esp32c3/rom/cache.h"
#include "esp_heap_caps_init.h"

#define TAG             "esp_ps"
#define RV_INT_NUM      2

#define PS_INTR_ATTR IRAM_ATTR __attribute__((noinline))

extern intptr_t _vector_table;

extern int _reserve_w1_dram_start, _reserve_w1_dram_end;
extern int _reserve_w1_iram_start, _reserve_w1_iram_end;
extern int _iram_end;

static esp_ps_intr_handler_t intr_func;

SOC_RESERVE_MEMORY_REGION((int)&_reserve_w1_dram_start, (int)&_reserve_w1_dram_end, world1_dram);
SOC_RESERVE_MEMORY_REGION((int)&_reserve_w1_iram_start, (int)&_reserve_w1_iram_end, world1_iram);

static void esp_ps_iram_int_en(uint8_t int_num)
{
    permc_ll_iram_enable_int();

    intr_matrix_set(PRO_CPU_NUM, permc_ll_iram_get_int_source_num(), int_num);
}

static void esp_ps_dram_int_en(uint8_t int_num)
{
    permc_ll_dram_enable_int();

    intr_matrix_set(PRO_CPU_NUM, permc_ll_dram_get_int_source_num(), int_num);
}

static void esp_ps_flash_cache_int_en(uint8_t int_num)
{
    permc_ll_flash_icache_enable_int();

    intr_matrix_set(PRO_CPU_NUM, permc_ll_flash_cache_get_int_source_num(), int_num);
}

static void esp_ps_pif_int_en(uint8_t int_num)
{
    permc_ll_pif_enable_int();

    intr_matrix_set(PRO_CPU_NUM, permc_ll_pif_get_int_source_num(), int_num);
}

static PS_INTR_ATTR void esp_ps_violation_intr_func(void *args)
{
    if (esp_ptr_executable(intr_func)) {
        intr_func(args);
    }

    esp_ps_handle_crashed_task();
}

static esp_err_t esp_ps_int_init(esp_ps_intr_handler_t fn)
{
    wcntl_ll_set_mtvec_base((uint32_t)&_vector_table);

    wcntl_ll_set_entry_check(0xFFFFFFFF);

    intr_handler_set(RV_INT_NUM, esp_ps_violation_intr_func, NULL);

    intr_func = fn;

    /* enable the interrupt in the INTC */
    esprv_intc_int_enable(BIT(RV_INT_NUM));
    esprv_intc_int_set_type(BIT(RV_INT_NUM), INTR_TYPE_LEVEL);
    esprv_intc_int_set_priority(RV_INT_NUM, 3);

    esp_ps_iram_int_en(RV_INT_NUM);
    esp_ps_dram_int_en(RV_INT_NUM);
    esp_ps_flash_cache_int_en(RV_INT_NUM);
    esp_ps_pif_int_en(RV_INT_NUM);

    return ESP_OK;
}

static void esp_ps_irom_config()
{
    permc_ll_irom_set_perm(PERMC_WORLD_0, PERMC_ACCESS_ALL);
    permc_ll_irom_set_perm(PERMC_WORLD_1, PERMC_ACCESS_NONE);
}

static void esp_ps_iram_config()
{
    permc_ll_icache_set_perm(PERMC_WORLD_0, PERMC_ACCESS_ALL);
    permc_ll_icache_set_perm(PERMC_WORLD_1, PERMC_ACCESS_NONE);

    permc_ll_iram_set_split_line(PERMC_SPLIT_LINE_0, (intptr_t)&_reserve_w1_iram_start);

    permc_ll_iram_set_split_line(PERMC_SPLIT_LINE_1, (intptr_t)&_reserve_w1_iram_start);

    permc_ll_iram_set_perm(PERMC_AREA_0, PERMC_WORLD_0, PERMC_ACCESS_ALL);
    permc_ll_iram_set_perm(PERMC_AREA_1, PERMC_WORLD_0, PERMC_ACCESS_ALL);
    permc_ll_iram_set_perm(PERMC_AREA_2, PERMC_WORLD_0, PERMC_ACCESS_ALL);

    permc_ll_iram_set_perm(PERMC_AREA_0, PERMC_WORLD_1, PERMC_ACCESS_NONE);
    permc_ll_iram_set_perm(PERMC_AREA_1, PERMC_WORLD_1, PERMC_ACCESS_ALL);
    permc_ll_iram_set_perm(PERMC_AREA_2, PERMC_WORLD_1, PERMC_ACCESS_ALL);

    /* PMS_3 corresponds to region after the main split line, i.e entire DRAM */
    permc_ll_iram_set_perm(PERMC_AREA_3, PERMC_WORLD_0, PERMC_ACCESS_NONE);
    permc_ll_iram_set_perm(PERMC_AREA_3, PERMC_WORLD_1, PERMC_ACCESS_NONE);
}

static void esp_ps_drom_config()
{
    permc_ll_drom_set_perm(PERMC_WORLD_0, PERMC_ACCESS_ALL);
    permc_ll_drom_set_perm(PERMC_WORLD_1, PERMC_ACCESS_NONE);
}

static void esp_ps_dram_config()
{
    permc_ll_dram_set_split_line(PERMC_SPLIT_LINE_0, (intptr_t)&_reserve_w1_dram_start);

    permc_ll_dram_set_split_line(PERMC_SPLIT_LINE_1, (intptr_t)&_reserve_w1_dram_start);

    permc_ll_dram_set_perm(PERMC_AREA_1, PERMC_WORLD_0, PERMC_ACCESS_ALL);
    permc_ll_dram_set_perm(PERMC_AREA_2, PERMC_WORLD_0, PERMC_ACCESS_ALL);
    permc_ll_dram_set_perm(PERMC_AREA_3, PERMC_WORLD_0, PERMC_ACCESS_ALL);

    permc_ll_dram_set_perm(PERMC_AREA_1, PERMC_WORLD_1, PERMC_ACCESS_NONE);
    permc_ll_dram_set_perm(PERMC_AREA_2, PERMC_WORLD_1, PERMC_ACCESS_ALL);
    permc_ll_dram_set_perm(PERMC_AREA_3, PERMC_WORLD_1, PERMC_ACCESS_ALL);

    /* PMS_0 corresponds to region before the main split line, i.e entire IRAM */
    permc_ll_dram_set_perm(PERMC_AREA_0, PERMC_WORLD_0, PERMC_ACCESS_NONE);
    permc_ll_dram_set_perm(PERMC_AREA_0, PERMC_WORLD_1, PERMC_ACCESS_NONE);
}

static void esp_ps_rtc_config()
{
    permc_ll_rtc_set_split_line(PERMC_WORLD_0, SOC_RTC_IRAM_LOW);
    permc_ll_rtc_set_split_line(PERMC_WORLD_1, SOC_RTC_IRAM_LOW);

    permc_ll_rtc_set_perm(PERMC_AREA_0, PERMC_WORLD_0, PERMC_ACCESS_ALL);
    permc_ll_rtc_set_perm(PERMC_AREA_1, PERMC_WORLD_0, PERMC_ACCESS_ALL);

    // No RTC access to WORLD1
    permc_ll_rtc_set_perm(PERMC_AREA_0, PERMC_WORLD_1, PERMC_ACCESS_NONE);
    permc_ll_rtc_set_perm(PERMC_AREA_1, PERMC_WORLD_1, PERMC_ACCESS_NONE);
}

static IRAM_ATTR void esp_ps_flash_cache_config()
{
    /* Invalidate Cache */
    Cache_Invalidate_ICache_All();

    permc_ll_flash_icache_set_split_line(PERMC_SPLIT_LINE_0, 0x42000000);
    permc_ll_flash_icache_set_split_line(PERMC_SPLIT_LINE_1, 0x42400000);
    permc_ll_flash_icache_set_split_line(PERMC_SPLIT_LINE_2, 0x42800000);

    permc_ll_flash_icache_set_perm(PERMC_AREA_0, PERMC_WORLD_0, PERMC_ACCESS_ALL);
    /* WORLD0 requires access to WORLD1 to load the cache when returning to WORLD1 from WORLD0 */
    permc_ll_flash_icache_set_perm(PERMC_AREA_1, PERMC_WORLD_0, PERMC_ACCESS_ALL);

    permc_ll_flash_icache_set_perm(PERMC_AREA_0, PERMC_WORLD_1, PERMC_ACCESS_NONE);
    permc_ll_flash_icache_set_perm(PERMC_AREA_1, PERMC_WORLD_1, PERMC_ACCESS_ALL);
}

static void esp_ps_revoke_world1_peripheral_permissions(void)
{
    esp_ps_set_periph_perm(PS_UART1, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_I2C, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_MISC, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_WDG, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_IO_MUX, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_RTC, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_TIMER, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_FE, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_FE2, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_GPIO, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_G0SPI_0, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_G0SPI_1, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_UART, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_SYSTIMER, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_TIMERGROUP1, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_TIMERGROUP, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_BB, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_LEDC, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_RMT, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_UHCI0, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_I2C_EXT0, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_BT, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_PWR, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_WIFIMAC, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_RWBT, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_I2S1, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_CAN, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_APB_CTRL, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_SPI_2, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_WORLD_CONTROLLER, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_DIO, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_AD, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_CACHE_CONFIG, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_DMA_COPY, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_INTERRUPT, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_SENSITIVE, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_SYSTEM, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_USB_DEVICE, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_BT_PWR, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_APB_ADC, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_CRYPTO_DMA, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_CRYPTO_PERI, PS_WORLD_1, PS_PERM_NONE);
    esp_ps_set_periph_perm(PS_USB_WRAP, PS_WORLD_1, PS_PERM_NONE);
}

esp_err_t esp_ps_init(esp_ps_intr_handler_t fn)
{
    if (esp_memprot_is_locked_any()) {
        ESP_LOGE(TAG, "Permission registers already configured");
        return ESP_FAIL;
    }

    esp_ps_int_init(fn);

    permc_ll_sram_set_split_line((intptr_t)&_iram_end);

    esp_ps_irom_config();

    esp_ps_drom_config();

    esp_ps_iram_config();

    esp_ps_dram_config();

    esp_ps_rtc_config();

    esp_ps_flash_cache_config();

    esp_ps_revoke_world1_peripheral_permissions();

    return ESP_OK;
}

IRAM_ATTR esp_err_t esp_ps_user_spawn_task(void *user_entry, uint32_t stack_sz)
{
    TaskHandle_t handle;

    if (!is_valid_user_i_addr((void *)user_entry)) {
        ESP_LOGE(TAG, "Incorrect user entry");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "User entry point: %p", user_entry);
    StaticTask_t *xtaskTCB = heap_caps_malloc(sizeof(StaticTask_t), portTcbMemoryCaps);
    if (xtaskTCB == NULL) {
        ESP_LOGE(TAG, "Failed to allocate user space..Skipping");
        return ESP_ERR_NO_MEM;
    }
    StackType_t *xtaskStack = heap_caps_malloc(stack_sz, MALLOC_CAP_WORLD1);
    if (xtaskStack == NULL) {
        ESP_LOGE(TAG, "Failed to allocate user space..Skipping");
        free(xtaskTCB);
        return ESP_ERR_NO_MEM;
    }

    StackType_t *kernel_stack = heap_caps_malloc(KERNEL_STACK_SIZE, portStackMemoryCaps);
    if (kernel_stack == NULL) {
        ESP_LOGE(TAG, "Failed to allocate kernel stack..Skipping");
        free(xtaskTCB);
        free(xtaskStack);
        return ESP_ERR_NO_MEM;
    }

    memset(kernel_stack, tskSTACK_FILL_BYTE, KERNEL_STACK_SIZE * sizeof( StackType_t ) );

    /* Suspend the scheduler to ensure that it does not switch to the newly created task.
     * We need to first set the kernel stack as a TLS and only then it should be executed
     */
    vTaskSuspendAll();
    handle = xTaskCreateStatic(user_entry, "User main task", stack_sz, NULL, 0, xtaskStack, xtaskTCB);

    vTaskSetThreadLocalStoragePointerAndDelCallback(handle, 1, kernel_stack, NULL);

    xTaskResumeAll();
    return ESP_OK;
}

esp_err_t esp_ps_user_set_entry(void *user_entry)
{
    if (!is_valid_user_i_addr(user_entry)) {
        return ESP_FAIL;
    }

    wcntl_ll_set_w1_entry((intptr_t)user_entry);

    return ESP_OK;
}

static void register_heap()
{
    usr_custom_app_desc_t app_desc;
    const esp_partition_t *user_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, "user_app");

    if (user_partition == NULL) {
        ESP_LOGE(TAG, "User code partition not found");
        return;
    }

    uint32_t offset = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);

    esp_partition_read(user_partition, offset, &app_desc, sizeof(usr_custom_app_desc_t));

    /* Sanity check. user_app_dram_start has to be equal to _reserve_w1_dram_start
     * If otherwise, probably a case of stale build. assert !
     */
    assert(app_desc.user_app_dram_start == (int)&_reserve_w1_dram_start);

    uint32_t w1_caps[] = {MALLOC_CAP_WORLD1, 0, 0};
    uint32_t w1_heap_size = (int)&_reserve_w1_dram_end - app_desc.user_app_heap_start;
    heap_caps_add_region_with_caps(w1_caps, app_desc.user_app_heap_start, (int)&_reserve_w1_dram_end);

    ESP_LOGI(TAG, "heap: At %08X len %08X (%d KiB): W1DRAM", app_desc.user_app_heap_start, w1_heap_size, w1_heap_size / 1024);
}

IRAM_ATTR esp_err_t esp_ps_user_boot()
{
    esp_image_metadata_t user_img_data = {0};
    esp_err_t ret = esp_ps_user_unpack(&user_img_data);
    if (ret == ESP_OK) {
        void *user_entry = (void *)user_img_data.image.entry_addr;
        ret = esp_ps_user_set_entry(user_entry);
        if (ret == ESP_OK) {
            register_heap();
            ret = esp_ps_user_spawn_task(user_entry, 4096);
        }
    }
     return ret;
}

static void cleanup_user_tasks()
{
    ets_printf("Cleaning up user tasks\n");
    TaskHandle_t handle = NULL;
    TaskSnapshot_t *snapshots = NULL;
    uint32_t task_count = uxTaskGetNumberOfTasks();
    snapshots = calloc(task_count, sizeof(TaskSnapshot_t));
    if (!snapshots) {
        ets_printf("Not enough memory to store task snapshots\n");
        return;
    }
    unsigned int tcb_size;
    uint32_t count = uxTaskGetSnapshotAll(snapshots, task_count, &tcb_size);
    for (int i = 0; i < count; i++) {
        handle = (TaskHandle_t)snapshots[i].pxTCB;
        if (pvTaskGetThreadLocalStoragePointer(handle, 1) == NULL) {
            // This indicates that its a protected space task.
            // Keep it as it is
            continue;
        }
        ets_printf("Deleting user task: %s\n", pcTaskGetTaskName(handle));
        vTaskDelete(handle);
    }
    free(snapshots);
}

static void oneshot_timer_callback(void* arg)
{
    esp_ps_user_boot();
}

static void reboot_user_app()
{
    const esp_timer_create_args_t oneshot_timer_args = {
        .callback = &oneshot_timer_callback,
        .arg = (void*) NULL,
        .name = "restart_user_app"
    };
    esp_timer_handle_t oneshot_timer;
    esp_timer_create(&oneshot_timer_args, &oneshot_timer);

    esp_timer_start_once(oneshot_timer, 1*1000*1000);
}

IRAM_ATTR void esp_ps_user_reboot()
{
    cleanup_user_tasks();
    reboot_user_app();
}

IRAM_ATTR char *esp_ps_int_type_to_str(esp_ps_int_t int_type)
{
    switch(int_type) {
        case PS_IRAM_INT:
            return "IRAM";
        case PS_DRAM_INT:
            return "DRAM";
        case PS_FLASH_ICACHE_INT:
            return "Flash Icache";
        case PS_RTC_INT:
            return "RTC";
        case PS_PERIPH_INT:
            return "Peripheral";
        default:
            return "Invalid";
    }
}

IRAM_ATTR void esp_ps_enable_int(esp_ps_int_t int_type)
{
    switch(int_type) {
        case PS_IRAM_INT:
            permc_ll_iram_enable_int();
            break;
        case PS_DRAM_INT:
            permc_ll_dram_enable_int();
            break;
        case PS_FLASH_ICACHE_INT:
            permc_ll_flash_icache_enable_int();
            break;
        case PS_RTC_INT:
        case PS_PERIPH_INT:
            permc_ll_pif_enable_int();
            break;
        default:
            ESP_LOGE(TAG, "Invalid interrupt type");
            break;
    }
}

IRAM_ATTR void esp_ps_clear_and_reenable_int(esp_ps_int_t int_type)
{
    switch(int_type) {
        case PS_IRAM_INT:
            permc_ll_iram_clear_int();
            permc_ll_iram_enable_int();
            break;
        case PS_DRAM_INT:
            permc_ll_dram_clear_int();
            permc_ll_dram_enable_int();
            break;
        case PS_FLASH_ICACHE_INT:
            permc_ll_flash_icache_clear_int();
            permc_ll_flash_icache_enable_int();
            break;
        case PS_RTC_INT:
        case PS_PERIPH_INT:
            permc_ll_pif_clear_int();
            permc_ll_pif_enable_int();
            break;
        default:
            ESP_LOGE(TAG, "Invalid interrupt type");
            break;
    }
}

IRAM_ATTR esp_ps_int_t esp_ps_get_int_status()
{
    esp_ps_int_t int_status = 0;

    if (permc_ll_iram_get_int_status()) {
        int_status = PS_IRAM_INT;
    } else if (permc_ll_dram_get_int_status()) {
        int_status = PS_DRAM_INT;
    } else if (permc_ll_flash_icache_get_int_status()) {
        int_status = PS_FLASH_ICACHE_INT;
    } else if (permc_ll_pif_get_int_status()) {
        uint32_t addr = permc_ll_pif_get_fault_addr();
        if (addr >= SOC_RTC_IRAM_LOW && addr < SOC_RTC_IRAM_HIGH) {
            int_status = PS_RTC_INT;
        } else {
            int_status = PS_PERIPH_INT;
        }
    }

    return int_status;
}

IRAM_ATTR uint32_t esp_ps_get_fault_addr(esp_ps_int_t int_type)
{
    switch (int_type) {
        case PS_IRAM_INT:
            return permc_ll_iram_get_fault_addr();
        case PS_DRAM_INT:
            return permc_ll_dram_get_fault_addr();
        case PS_FLASH_ICACHE_INT:
            return permc_ll_flash_icache_get_fault_addr();
        case PS_RTC_INT:
        case PS_PERIPH_INT:
            return permc_ll_pif_get_fault_addr();
        default:
            return 0xFFFFFFFF;
    }
}

esp_err_t esp_ps_set_periph_perm(esp_ps_periph_t periph, esp_ps_world_t world, esp_ps_perm_t perm)
{
    permc_ll_pif_set_perm(periph, world, perm);
    return ESP_OK;
}
