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

#include <string.h>
#include <esp_image_format.h>
#include <esp_spi_flash.h>
#include <esp_partition.h>
#include "esp_log.h"
#include "esp_intr_alloc.h"
#include "esp_priv_access.h"
#include "esp_priv_access_priv.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
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

#if CONFIG_IDF_TARGET_ESP32C3
#if defined(IDF_VERSION_V4_3)
#include "esp32c3/memprot.h"
#elif defined(IDF_VERSION_V4_4)
#include "esp_memprot.h"
#endif
#include "riscv/interrupt.h"
#include "esp32c3/rom/cache.h"
#include "esp32c3/rom/rom_layout.h"
#elif CONFIG_IDF_TARGET_ESP32S3
#include "esp32s3/rom/cache.h"
#include "esp32s3/rom/rom_layout.h"
#include "esp_memprot.h"
#endif

#include "esp_heap_caps_init.h"

#ifdef CONFIG_PA_ENABLE_USER_APP_SECURE_BOOT
#define USER_BOOT_TASK_STACK_SZ         5120
#else
#define USER_BOOT_TASK_STACK_SZ         2560
#endif

#define USER_BOOT_TASK_PRIO             10

#if CONFIG_IDF_TARGET_ESP32C3
#define RV_INT_NUM      2
#elif CONFIG_IDF_TARGET_ESP32S3
// As per soc.h, interrupt number 18 is free to use and generates level 1 interrupt
#define RV_INT_NUM      18
#endif

#define FLASH_CACHE_PAGE_SIZE           0x10000

#define PA_INTR_ATTR IRAM_ATTR __attribute__((noinline))

typedef struct {
    SemaphoreHandle_t semph_handle;
    esp_err_t ret;
} user_boot_status_t;

static const char *TAG = "esp_priv_access";

extern intptr_t _vector_table;

extern int _reserve_w1_dram_start, _reserve_w1_dram_end;
extern int _reserve_w1_iram_start, _reserve_w1_iram_end;
extern int _iram_end;

/* API to start user task */
extern esp_err_t esp_syscall_spawn_user_task(void *user_entry, int stack_sz, usr_custom_app_desc_t *app_desc);
/* API to cleanup user resources */
extern void esp_syscall_clear_user_resourses(void);

/* User interrupt/exception handler provided while initializing */
static esp_priv_access_intr_handler_t intr_func;

/* Copy of user app descriptor read from flash */
static usr_custom_app_desc_t app_desc;

SOC_RESERVE_MEMORY_REGION((int)&_reserve_w1_dram_start, (int)&_reserve_w1_dram_end, world1_dram);
/* For S3, W1 IRAM section gets reserved by default inside memory_layout.c */
#if CONFIG_IDF_TARGET_ESP32C3
SOC_RESERVE_MEMORY_REGION((int)&_reserve_w1_iram_start, (int)&_reserve_w1_iram_end, world1_iram);
#endif

/* Enable IRAM permission violation interrupt */
static void esp_priv_access_iram_int_en(uint8_t int_num)
{
    permc_ll_iram_enable_int();

    intr_matrix_set(PRO_CPU_NUM, permc_ll_iram_get_int_source_num(), int_num);
}

/* Enable DRAM permission violation interrupt */
static void esp_priv_access_dram_int_en(uint8_t int_num)
{
    permc_ll_dram_enable_int();

    intr_matrix_set(PRO_CPU_NUM, permc_ll_dram_get_int_source_num(), int_num);
}

/* Enable Flash cache permission violation interrupt */
static void esp_priv_access_flash_cache_int_en(uint8_t int_num)
{
    permc_ll_flash_icache_enable_int();
    permc_ll_flash_dcache_enable_int();

    intr_matrix_set(PRO_CPU_NUM, permc_ll_flash_cache_get_int_source_num(), int_num);
}

/* Enable PIF bus permission violation interrupt */
static void esp_priv_access_pif_int_en(uint8_t int_num)
{
    permc_ll_pif_enable_int();

    intr_matrix_set(PRO_CPU_NUM, permc_ll_pif_get_int_source_num(), int_num);
}

/* Wrapper permission violation interrupt handler.
 * It calls protected app registered interrupt handler, if available.
 * It then cleanly handles the violation and prints information on console
 */
static PA_INTR_ATTR void esp_priv_access_violation_intr_func(void *args)
{
    if (esp_ptr_executable(intr_func)) {
        intr_func(args);
    }

#if CONFIG_IDF_TARGET_ESP32C3
    esp_priv_access_handle_crashed_task();
#endif
}

/* Configure top level permission violation interrupt,
 * Set WORLD controller monitor registers
 */
static esp_err_t esp_priv_access_int_init(esp_priv_access_intr_handler_t fn)
{
#if CONFIG_IDF_TARGET_ESP32C3
    wcntl_ll_set_mtvec_base((uint32_t)&_vector_table);

    wcntl_ll_set_entry_check(0xFFFFFFFF);

    intr_handler_set(RV_INT_NUM, esp_priv_access_violation_intr_func, NULL);

    intr_func = fn;

    /* enable the interrupt in the INTC */
    esprv_intc_int_enable(BIT(RV_INT_NUM));
    esprv_intc_int_set_type(BIT(RV_INT_NUM), INTR_TYPE_LEVEL);
    esprv_intc_int_set_priority(RV_INT_NUM, 3);
#elif CONFIG_IDF_TARGET_ESP32S3
    /* We use two separate vector tables for WORLD0 and WORLD1,
     * this allows us to handle Windowed exceptions in WORLD1 itself, thus saving CPU cycles.
     * For other vectors, we jump to WORLD0 vector table from WORLD1 vector table
     *
     * The vector table for WORLD1 is placed right at the start of WORLD1 IRAM
     */
    wcntl_ll_set_vecbase(WCNTL_WORLD_0, (uint32_t)&_vector_table);
    wcntl_ll_set_vecbase(WCNTL_WORLD_1, (uint32_t)&_reserve_w1_iram_start);

    extern void _UserExceptionVector(void);
    wcntl_ll_set_entry_check(1, (uint32_t)_UserExceptionVector);

    xt_set_interrupt_handler(RV_INT_NUM, esp_priv_access_violation_intr_func, NULL);
    intr_func = fn;
    xt_ints_on(BIT(RV_INT_NUM));
#endif

    esp_priv_access_iram_int_en(RV_INT_NUM);
    esp_priv_access_dram_int_en(RV_INT_NUM);
    esp_priv_access_flash_cache_int_en(RV_INT_NUM);
    esp_priv_access_pif_int_en(RV_INT_NUM);

    return ESP_OK;
}

/* Configure IROM permissions */
static void esp_priv_access_irom_config()
{
    permc_ll_irom_set_perm(PERMC_WORLD_0, PERMC_ACCESS_ALL);
    permc_ll_irom_set_perm(PERMC_WORLD_1, PERMC_ACCESS_NONE);
}

/* Configure IRAM permissions */
static void esp_priv_access_iram_config()
{
#if CONFIG_IDF_TARGET_ESP32C3
    permc_ll_icache_set_perm(PERMC_WORLD_0, PERMC_ACCESS_ALL);
    permc_ll_icache_set_perm(PERMC_WORLD_1, PERMC_ACCESS_NONE);
#elif CONFIG_IDF_TARGET_ESP32S3
    permc_ll_icache_set_perm(PERMC_AREA_0, PERMC_WORLD_0, PERMC_ACCESS_ALL);
    permc_ll_icache_set_perm(PERMC_AREA_1, PERMC_WORLD_0, PERMC_ACCESS_ALL);

    permc_ll_icache_set_perm(PERMC_AREA_0, PERMC_WORLD_1, PERMC_ACCESS_NONE);
#if CONFIG_ESP32S3_INSTRUCTION_CACHE_16KB
    permc_ll_icache_set_perm(PERMC_AREA_1, PERMC_WORLD_1, PERMC_ACCESS_ALL);
#else
    permc_ll_icache_set_perm(PERMC_AREA_1, PERMC_WORLD_1, PERMC_ACCESS_NONE);
#endif
#endif

    permc_ll_iram_set_split_line(PERMC_SPLIT_LINE_0, (intptr_t)&_reserve_w1_iram_end);

    permc_ll_iram_set_split_line(PERMC_SPLIT_LINE_1, (intptr_t)&_reserve_w1_iram_end);

    permc_ll_iram_set_perm(PERMC_AREA_0, PERMC_WORLD_0, PERMC_ACCESS_ALL);
    permc_ll_iram_set_perm(PERMC_AREA_1, PERMC_WORLD_0, PERMC_ACCESS_ALL);
    permc_ll_iram_set_perm(PERMC_AREA_2, PERMC_WORLD_0, PERMC_ACCESS_ALL);

#if CONFIG_ESP32S3_INSTRUCTION_CACHE_16KB
    permc_ll_iram_set_perm(PERMC_AREA_0, PERMC_WORLD_1, PERMC_ACCESS_NONE);
#else
    permc_ll_iram_set_perm(PERMC_AREA_0, PERMC_WORLD_1, PERMC_ACCESS_ALL);
#endif
    permc_ll_iram_set_perm(PERMC_AREA_1, PERMC_WORLD_1, PERMC_ACCESS_NONE);
    permc_ll_iram_set_perm(PERMC_AREA_2, PERMC_WORLD_1, PERMC_ACCESS_NONE);

    /* PMS_3 corresponds to region after the main split line, i.e entire DRAM */
    permc_ll_iram_set_perm(PERMC_AREA_3, PERMC_WORLD_0, PERMC_ACCESS_NONE);
    permc_ll_iram_set_perm(PERMC_AREA_3, PERMC_WORLD_1, PERMC_ACCESS_NONE);
}

/* Configure DROM permissions */
static void esp_priv_access_drom_config()
{
    permc_ll_drom_set_perm(PERMC_WORLD_0, PERMC_ACCESS_ALL);
    permc_ll_drom_set_perm(PERMC_WORLD_1, PERMC_ACCESS_NONE);
}

/* Configure DRAM permissions */
static void esp_priv_access_dram_config()
{
#if CONFIG_IDF_TARGET_ESP32S3
    permc_ll_dcache_set_perm(PERMC_WORLD_0, PERMC_ACCESS_ALL);
    permc_ll_dcache_set_perm(PERMC_WORLD_1, PERMC_ACCESS_NONE);
#endif

    permc_ll_dram_set_split_line(PERMC_SPLIT_LINE_0, (intptr_t)&_reserve_w1_dram_start);

    /* Split line to protect DRAM area reserved for ROM functions.
     * ALIGN_DOWN macro is used to align the address to 256 bit boundary */
    const ets_rom_layout_t *layout = ets_rom_layout_p;
    intptr_t rom_reserve_aligned = ALIGN_DOWN((int)layout->dram0_rtos_reserved_start, 256);
    permc_ll_dram_set_split_line(PERMC_SPLIT_LINE_1, rom_reserve_aligned);

    permc_ll_dram_set_perm(PERMC_AREA_1, PERMC_WORLD_0, PERMC_ACCESS_ALL);
    permc_ll_dram_set_perm(PERMC_AREA_2, PERMC_WORLD_0, PERMC_ACCESS_ALL);
    permc_ll_dram_set_perm(PERMC_AREA_3, PERMC_WORLD_0, PERMC_ACCESS_ALL);

    permc_ll_dram_set_perm(PERMC_AREA_1, PERMC_WORLD_1, PERMC_ACCESS_NONE);
    permc_ll_dram_set_perm(PERMC_AREA_2, PERMC_WORLD_1, PERMC_ACCESS_ALL);
    permc_ll_dram_set_perm(PERMC_AREA_3, PERMC_WORLD_1, PERMC_ACCESS_NONE);

    /* PMS_0 corresponds to region before the main split line, i.e entire IRAM */
    permc_ll_dram_set_perm(PERMC_AREA_0, PERMC_WORLD_0, PERMC_ACCESS_NONE);
    permc_ll_dram_set_perm(PERMC_AREA_0, PERMC_WORLD_1, PERMC_ACCESS_NONE);
}

/* Configure RTC permissions */
static void esp_priv_access_rtc_config()
{
    permc_ll_rtc_set_split_line(PERMC_WORLD_0, SOC_RTC_IRAM_LOW);
    permc_ll_rtc_set_split_line(PERMC_WORLD_1, SOC_RTC_IRAM_LOW);

    permc_ll_rtc_set_perm(PERMC_AREA_0, PERMC_WORLD_0, PERMC_ACCESS_ALL);
    permc_ll_rtc_set_perm(PERMC_AREA_1, PERMC_WORLD_0, PERMC_ACCESS_ALL);

    // No RTC access to WORLD1
    permc_ll_rtc_set_perm(PERMC_AREA_0, PERMC_WORLD_1, PERMC_ACCESS_NONE);
    permc_ll_rtc_set_perm(PERMC_AREA_1, PERMC_WORLD_1, PERMC_ACCESS_NONE);
}

#if CONFIG_IDF_TARGET_ESP32C3
/* Configure Flash cache permissions */
static IRAM_ATTR void esp_priv_access_flash_icache_config()
{
    /* Invalidate Cache */
    Cache_Invalidate_ICache_All();

    /* Configure Icache */
    permc_ll_flash_icache_set_split_line(PERMC_SPLIT_LINE_0, 0x42000000);
    permc_ll_flash_icache_set_split_line(PERMC_SPLIT_LINE_1, 0x42400000);
    permc_ll_flash_icache_set_split_line(PERMC_SPLIT_LINE_2, 0x42800000);

    permc_ll_flash_icache_set_perm(PERMC_AREA_0, PERMC_WORLD_0, PERMC_ACCESS_ALL);
    /* WORLD0 requires access to WORLD1 to load the cache when returning to WORLD1 from WORLD0 */
    permc_ll_flash_icache_set_perm(PERMC_AREA_1, PERMC_WORLD_0, PERMC_ACCESS_ALL);

    permc_ll_flash_icache_set_perm(PERMC_AREA_0, PERMC_WORLD_1, PERMC_ACCESS_NONE);
    permc_ll_flash_icache_set_perm(PERMC_AREA_1, PERMC_WORLD_1, PERMC_ACCESS_ALL);

}

static IRAM_ATTR void esp_priv_access_flash_dcache_config()
{
    /* Configure Dcache */
    permc_ll_flash_dcache_set_split_line(PERMC_SPLIT_LINE_0, 0x3C000000);
    permc_ll_flash_dcache_set_split_line(PERMC_SPLIT_LINE_1, 0x3C400000);
    permc_ll_flash_dcache_set_split_line(PERMC_SPLIT_LINE_2, 0x3C800000);

    permc_ll_flash_dcache_set_perm(PERMC_AREA_0, PERMC_WORLD_0, PERMC_ACCESS_ALL);
    /* WORLD0 requires access to WORLD1 to load the cache when returning to WORLD1 from WORLD0 */
    permc_ll_flash_dcache_set_perm(PERMC_AREA_1, PERMC_WORLD_0, PERMC_ACCESS_ALL);

    permc_ll_flash_dcache_set_perm(PERMC_AREA_0, PERMC_WORLD_1, PERMC_ACCESS_NONE);
    permc_ll_flash_dcache_set_perm(PERMC_AREA_1, PERMC_WORLD_1, PERMC_ACCESS_ALL);
}

#elif CONFIG_IDF_TARGET_ESP32S3

/* Configure Flash cache permissions */

/* Flash cache configuration on S3 is done for _physical_ pages of flash and it applies to both Icache and Dcache.
 * So the partition table must have the user app partitions at the end and together.
 */
static IRAM_ATTR void esp_priv_access_flash_cache_config()
{
    uint32_t u0_addr, u0_size, u1_size;
    const esp_partition_t *user_0_part = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_USER_0, NULL);
    if (!user_0_part) {
        ESP_LOGE(TAG, "User app partition not found");
        abort();
    }

    u0_addr = user_0_part->address;
    u0_size = user_0_part->size;

    const esp_partition_t *user_1_part = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_USER_1, NULL);
    if (!user_1_part) {
        u1_size = 0;
    } else {
        u1_size = user_1_part->size;
    }

    /* Invalidate Cache */
    uint32_t autoload = Cache_Suspend_DCache();
    Cache_WriteBack_All();
    Cache_Invalidate_DCache_All();
    Cache_Invalidate_ICache_All();

    uint32_t total_user_size = u0_size + u1_size;
    /* Total addressable space is 1GB */
    uint32_t remaining_size = 0x40000000 - (u0_addr + total_user_size);

    uint32_t line0_start_addr = 0x0;
    uint32_t line1_start_addr = u0_addr;
    uint32_t line2_start_addr = u0_addr + total_user_size;
    uint32_t line3_start_addr = u0_addr + total_user_size + (remaining_size / 2);

    /* Configure cache */
    permc_ll_flash_cache_set_split_line(PERMC_SPLIT_LINE_0, line0_start_addr, u0_addr / FLASH_CACHE_PAGE_SIZE);
    permc_ll_flash_cache_set_split_line(PERMC_SPLIT_LINE_1, line1_start_addr, total_user_size / FLASH_CACHE_PAGE_SIZE);
    permc_ll_flash_cache_set_split_line(PERMC_SPLIT_LINE_2, line2_start_addr, (remaining_size / 2) / FLASH_CACHE_PAGE_SIZE);
    permc_ll_flash_cache_set_split_line(PERMC_SPLIT_LINE_3, line3_start_addr, (remaining_size / 2) / FLASH_CACHE_PAGE_SIZE);

    permc_ll_flash_cache_set_perm(PERMC_AREA_0, PERMC_WORLD_0, PERMC_ACCESS_ALL);
    /* WORLD0 requires access to WORLD1 to load the cache when returning to WORLD1 from WORLD0 */
    permc_ll_flash_cache_set_perm(PERMC_AREA_1, PERMC_WORLD_0, PERMC_ACCESS_ALL);
    permc_ll_flash_cache_set_perm(PERMC_AREA_2, PERMC_WORLD_0, PERMC_ACCESS_ALL);
    permc_ll_flash_cache_set_perm(PERMC_AREA_3, PERMC_WORLD_0, PERMC_ACCESS_ALL);

    permc_ll_flash_cache_set_perm(PERMC_AREA_0, PERMC_WORLD_1, PERMC_ACCESS_NONE);
    permc_ll_flash_cache_set_perm(PERMC_AREA_1, PERMC_WORLD_1, PERMC_ACCESS_ALL);
    permc_ll_flash_cache_set_perm(PERMC_AREA_2, PERMC_WORLD_1, PERMC_ACCESS_NONE);
    permc_ll_flash_cache_set_perm(PERMC_AREA_3, PERMC_WORLD_1, PERMC_ACCESS_NONE);

    Cache_Resume_DCache(autoload);
}
#endif

/* Revoke access to all the peripherals for WORLD1 */
static void esp_priv_access_revoke_world1_peripheral_permissions(void)
{
    esp_priv_access_set_periph_perm(PA_UART1, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_I2C, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_MISC, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_WDG, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_IO_MUX, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_RTC, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_TIMER, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_FE, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_FE2, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_GPIO, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_G0SPI_0, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_G0SPI_1, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_UART, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_SYSTIMER, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_TIMERGROUP1, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_TIMERGROUP, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_BB, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_LEDC, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_RMT, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_UHCI0, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_I2C_EXT0, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_BT, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_PWR, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_WIFIMAC, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_RWBT, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_I2S1, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_CAN, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_APB_CTRL, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_SPI_2, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_WORLD_CONTROLLER, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_DIO, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_AD, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_CACHE_CONFIG, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_DMA_COPY, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_INTERRUPT, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_SENSITIVE, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_SYSTEM, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_USB_DEVICE, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_BT_PWR, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_APB_ADC, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_CRYPTO_DMA, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_CRYPTO_PERI, PA_WORLD_1, PA_PERM_NONE);
    esp_priv_access_set_periph_perm(PA_USB_WRAP, PA_WORLD_1, PA_PERM_NONE);
}

esp_err_t esp_priv_access_init(esp_priv_access_intr_handler_t fn)
{
#ifdef CONFIG_ESP_SYSTEM_MEMPROT_FEATURE
#if defined(IDF_VERSION_V4_3)
    if (esp_memprot_is_locked_any()) {
#elif defined(IDF_VERSION_V4_4)
    bool mprot_status = false;
    esp_err_t err = esp_mprot_is_conf_locked_any(&mprot_status);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to check PMS setting lock, 0x%x", err);
        return ESP_FAIL;
    }
    if (mprot_status) {
#endif
        ESP_LOGE(TAG, "Permission registers already configured");
        return ESP_FAIL;
    }
#endif

    esp_priv_access_int_init(fn);

    permc_ll_sram_set_split_line((intptr_t)&_iram_end);

    esp_priv_access_irom_config();

    esp_priv_access_drom_config();

    esp_priv_access_iram_config();

    esp_priv_access_dram_config();

    esp_priv_access_rtc_config();

#if CONFIG_IDF_TARGET_ESP32C3
    esp_priv_access_flash_icache_config();

    esp_priv_access_flash_dcache_config();
#elif CONFIG_IDF_TARGET_ESP32S3
    esp_priv_access_flash_cache_config();
#endif

    esp_priv_access_revoke_world1_peripheral_permissions();

    return ESP_OK;
}

IRAM_ATTR esp_err_t esp_priv_access_user_spawn_task(void *user_entry, uint32_t stack_sz)
{
    return esp_syscall_spawn_user_task(user_entry, stack_sz, &app_desc);
}

esp_err_t esp_priv_access_user_set_entry(void *user_entry)
{
    if (!is_valid_user_i_addr(user_entry)) {
        return ESP_FAIL;
    }

    wcntl_ll_set_w1_entry((intptr_t)user_entry);

    return ESP_OK;
}

static void user_boot_task(void *args)
{
    user_boot_status_t *boot_status = (user_boot_status_t *)args;
    esp_image_metadata_t user_img_data = {0};
    esp_err_t ret = esp_priv_access_user_unpack(&user_img_data);
    if (ret == ESP_OK) {
        void *user_entry = (void *)user_img_data.image.entry_addr;
        ret = esp_priv_access_user_set_entry(user_entry);
        if (ret == ESP_OK) {
            esp_priv_access_load_user_app_desc(&app_desc);
            /* Sanity check. user_app_dram_start has to be equal to _reserve_w1_dram_start
             * If otherwise, probably a case of stale build. assert !
             */
            assert(app_desc.user_app_dram_start == (int)&_reserve_w1_dram_start);
            ret = esp_priv_access_user_spawn_task(user_entry, CONFIG_PA_USER_MAIN_TASK_STACK_SIZE);
        }
    }

    boot_status->ret = ret;
    xSemaphoreGive(boot_status->semph_handle);
    vTaskDelete(NULL);
}

esp_err_t esp_priv_access_user_boot()
{
    user_boot_status_t boot_status;
    boot_status.ret = ESP_FAIL;
    boot_status.semph_handle = xSemaphoreCreateBinary();
    if (boot_status.semph_handle == NULL) {
        ESP_LOGE(TAG, "Error creating notifying semaphore");
        return ESP_FAIL;
    }

    /* User boot needs to happen from a separate task because
     * secure boot verification requires large stack space.
     * We pass a struct which contains the semaphore and a variable that will
     * reflect the status (success or failure) of the user app boot
     */
    if (xTaskCreate(user_boot_task, "User boot task", USER_BOOT_TASK_STACK_SZ, &boot_status, USER_BOOT_TASK_PRIO, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Error creating user boot task");
        vSemaphoreDelete(boot_status.semph_handle);
        return ESP_FAIL;
    }

    /* Wait for user boot task to finish */
    xSemaphoreTake(boot_status.semph_handle, portMAX_DELAY);

    vSemaphoreDelete(boot_status.semph_handle);

    return boot_status.ret;
}

/* Routine to suspend all the user space tasks */
static void suspend_user_tasks()
{
    ets_printf("Suspending user tasks\n");
    TaskHandle_t handle = NULL;
    TaskSnapshot_t *snapshots = NULL;
    uint32_t task_count = uxTaskGetNumberOfTasks();
    /* Calloc is called from ISR context and can fail in some scenarios */
    snapshots = calloc(task_count, sizeof(TaskSnapshot_t));
    if (!snapshots) {
        ets_printf("Not enough memory to store task snapshots\n");
        return;
    }
    unsigned int tcb_size;
    uint32_t count = uxTaskGetSnapshotAll(snapshots, task_count, &tcb_size);
    for (int i = 0; i < count; i++) {
        handle = (TaskHandle_t)snapshots[i].pxTCB;
        if (pvTaskGetThreadLocalStoragePointer(handle, ESP_PA_TLS_OFFSET_KERN_STACK) == NULL) {
            // This indicates that its a protected space task.
            // Keep it as it is
            continue;
        }
        ets_printf("Suspending user task: %s\n", pcTaskGetTaskName(handle));
        vTaskSuspend(handle);
    }
    free(snapshots);
}

/* Timer callback to boot the user app from esp_timer task (Protected) context */
static void oneshot_timer_callback(void* arg)
{
    esp_syscall_clear_user_resourses();
    esp_priv_access_user_boot();
}

/* Reboot the user app without rebooting the protected app */
static void reboot_user_app()
{
    const esp_timer_create_args_t oneshot_timer_args = {
        .callback = &oneshot_timer_callback,
        .arg = (void*) NULL,
        .name = "restart_user_app"
    };
    esp_timer_handle_t oneshot_timer;
    esp_timer_create(&oneshot_timer_args, &oneshot_timer);

    esp_timer_start_once(oneshot_timer, 0);
}

IRAM_ATTR void esp_priv_access_user_reboot()
{
    suspend_user_tasks();
    reboot_user_app();
}

IRAM_ATTR char *esp_priv_access_int_type_to_str(esp_priv_access_int_t int_type)
{
    switch(int_type) {
        case PA_IRAM_INT:
            return "IRAM";
        case PA_DRAM_INT:
            return "DRAM";
        case PA_FLASH_ICACHE_INT:
            return "Flash Icache";
        case PA_FLASH_DCACHE_INT:
            return "Flash Dcache";
        case PA_RTC_INT:
            return "RTC";
        case PA_PERIPH_INT:
            return "Peripheral";
        default:
            return "Invalid";
    }
}

IRAM_ATTR void esp_priv_access_enable_int(esp_priv_access_int_t int_type)
{
    switch(int_type) {
        case PA_IRAM_INT:
            permc_ll_iram_enable_int();
            break;
        case PA_DRAM_INT:
            permc_ll_dram_enable_int();
            break;
        case PA_FLASH_ICACHE_INT:
            permc_ll_flash_icache_enable_int();
            break;
        case PA_FLASH_DCACHE_INT:
            permc_ll_flash_dcache_enable_int();
            break;
        case PA_RTC_INT:
        case PA_PERIPH_INT:
            permc_ll_pif_enable_int();
            break;
        default:
            ESP_LOGE(TAG, "Invalid interrupt type");
            break;
    }
}

IRAM_ATTR void esp_priv_access_clear_and_reenable_int(esp_priv_access_int_t int_type)
{
    switch(int_type) {
        case PA_IRAM_INT:
            permc_ll_iram_clear_int();
            permc_ll_iram_enable_int();
            break;
        case PA_DRAM_INT:
            permc_ll_dram_clear_int();
            permc_ll_dram_enable_int();
            break;
        case PA_FLASH_ICACHE_INT:
            permc_ll_flash_icache_clear_int();
            permc_ll_flash_icache_enable_int();
            break;
        case PA_FLASH_DCACHE_INT:
            permc_ll_flash_dcache_clear_int();
            permc_ll_flash_dcache_enable_int();
            break;
        case PA_RTC_INT:
        case PA_PERIPH_INT:
            permc_ll_pif_clear_int();
            permc_ll_pif_enable_int();
            break;
        default:
            ESP_LOGE(TAG, "Invalid interrupt type");
            break;
    }
}

IRAM_ATTR esp_priv_access_int_t esp_priv_access_get_int_status()
{
    esp_priv_access_int_t int_status = 0;

    if (permc_ll_iram_get_int_status()) {
        int_status = PA_IRAM_INT;
    } else if (permc_ll_dram_get_int_status()) {
        int_status = PA_DRAM_INT;
    } else if (permc_ll_flash_icache_get_int_status()) {
        int_status = PA_FLASH_ICACHE_INT;
    } else if  (permc_ll_flash_dcache_get_int_status()) {
        int_status = PA_FLASH_DCACHE_INT;
    } else if (permc_ll_pif_get_int_status()) {
        uint32_t addr = permc_ll_pif_get_fault_addr();
        if (addr >= SOC_RTC_IRAM_LOW && addr < SOC_RTC_IRAM_HIGH) {
            int_status = PA_RTC_INT;
        } else {
            int_status = PA_PERIPH_INT;
        }
    }

    return int_status;
}

IRAM_ATTR uint32_t esp_priv_access_get_fault_addr(esp_priv_access_int_t int_type)
{
    switch (int_type) {
        case PA_IRAM_INT:
            return permc_ll_iram_get_fault_addr();
        case PA_DRAM_INT:
            return permc_ll_dram_get_fault_addr();
        case PA_FLASH_ICACHE_INT:
            return permc_ll_flash_icache_get_fault_addr();
        case PA_FLASH_DCACHE_INT:
            return permc_ll_flash_dcache_get_fault_addr();
        case PA_RTC_INT:
        case PA_PERIPH_INT:
            return permc_ll_pif_get_fault_addr();
        default:
            return 0xFFFFFFFF;
    }
}

esp_err_t esp_priv_access_set_periph_perm(esp_priv_access_periph_t periph, esp_priv_access_world_t world, esp_priv_access_perm_t perm)
{
    permc_ll_pif_set_perm(periph, world, perm);
    return ESP_OK;
}
