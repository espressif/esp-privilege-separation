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

#pragma once

#include "soc/periph_defs.h"
#include "soc/world_controller_reg.h"
#include "soc/sensitive_reg.h"
#include "soc/interrupt_reg.h"
#include "soc/extmem_reg.h"
#include "soc/apb_ctrl_reg.h"
#include "esp_intr_alloc.h"
#include "hal/memprot_ll.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PERMC_WORLD_0 = 0,
    PERMC_WORLD_1,
} permc_world_t;

typedef enum {
    PERMC_SPLIT_LINE_0 = 0,
    PERMC_SPLIT_LINE_1,
    PERMC_SPLIT_LINE_2,
} permc_split_line_t;

typedef enum {
    PERMC_AREA_0 = 0,    /* Area between BASE and split_line 0 */
    PERMC_AREA_1,        /* Area between split_line 0 and split_line 1 */
    PERMC_AREA_2,        /* Area between split_line 1 and END */
    PERMC_AREA_3,
    PERMC_AREA_INVALID,
} permc_sram_area_t;

typedef enum {
    PERMC_ACCESS_NONE = 0,
    PERMC_ACCESS_R = 1,
    PERMC_ACCESS_W = 2,
    PERMC_ACCESS_X = 4,
    PERMC_ACCESS_ALL = 7,
} permc_flags_t;

/* There are 44 peripherals, each having 2 bits for permission configuration.
 * These are spread across 4 registers, each register having max. 16 peripheral entries
 *
 * Enum defined as per the bit field position of the peripheral in the register
 * FIELD = 30 - 2 * (ENUM % 16) */
typedef enum {
    PERMC_UART1 = 0,
    PERMC_I2C = 2,
    PERMC_MISC,
    PERMC_WDG = 6,
    PERMC_IO_MUX,
    PERMC_RTC,
    PERMC_TIMER,
    PERMC_FE,
    PERMC_FE2,
    PERMC_GPIO,
    PERMC_G0SPI_0,
    PERMC_G0SPI_1,
    PERMC_UART,
    PERMC_SYSTIMER,
    PERMC_TIMERGROUP1,
    PERMC_TIMERGROUP,
    PERMC_BB = 20,
    PERMC_LEDC = 23,
    PERMC_RMT = 26,
    PERMC_UHCI0 = 28,
    PERMC_I2C_EXT0,
    PERMC_BT = 31,
    PERMC_PWR = 33,
    PERMC_WIFIMAC,
    PERMC_RWBT = 36,
    PERMC_I2S1 = 40,
    PERMC_CAN = 42,
    PERMC_APB_CTRL = 45,
    PERMC_SPI_2 = 47,
    PERMC_WORLD_CONTROLLER,
    PERMC_DIO,
    PERMC_AD,
    PERMC_CACHE_CONFIG,
    PERMC_DMA_COPY,
    PERMC_INTERRUPT,
    PERMC_SENSITIVE,
    PERMC_SYSTEM,
    PERMC_USB_DEVICE,
    PERMC_BT_PWR,
    PERMC_APB_ADC = 59,
    PERMC_CRYPTO_DMA,
    PERMC_CRYPTO_PERI,
    PERMC_USB_WRAP,
    PERMC_MAX,
} permc_pif_t;

#define IRAM_PMS_W0_BASE       0
#define IRAM_PMS_W1_BASE       0
#define IRAM_PMS_S             3
#define IRAM_PMS_V             7

#define DRAM_PMS_W0_BASE        0
#define DRAM_PMS_W1_BASE       12
#define DRAM_PMS_S              2
#define DRAM_PMS_V              3

#define RTC_PMS_W0_BASE         0
#define RTC_PMS_W1_BASE         6
#define RTC_PMS_S               3
#define RTC_PMS_V               7

#define FLASH_ICACHE_V          3
#define FLASH_DCACHE_V          1

#define PIF_PMS_MAX_REG_ENTRY   16
#define PIF_PMS_V               3

#define permc_ll_rtc_get_int_source_num       permc_ll_pif_get_int_source_num
#define permc_ll_rtc_enable_int               permc_ll_pif_enable_int
#define permc_ll_rtc_clear_int                permc_ll_pif_clear_int
#define permc_ll_rtc_disable_int              permc_ll_pif_disable_int
#define permc_ll_rtc_get_int_status           permc_ll_pif_get_int_status
#define permc_ll_rtc_get_fault_wr             permc_ll_pif_get_fault_wr
#define permc_ll_rtc_get_fault_loadstore      permc_ll_pif_get_fault_loadstore
#define permc_ll_rtc_get_fault_world          permc_ll_pif_get_fault_world
#define permc_ll_rtc_get_fault_addr           permc_ll_pif_get_fault_addr

static inline void permc_ll_sram_set_split_line(intptr_t addr)
{
}

/*
 *  IRAM
 */
static inline void permc_ll_irom_set_perm(permc_world_t world, permc_flags_t flags)
{
}

static inline void permc_ll_icache_set_perm(permc_world_t world, permc_flags_t flags)
{
}

static inline void permc_ll_iram_set_split_line(permc_split_line_t line, intptr_t addr)
{
}

static inline void permc_ll_iram_set_perm(permc_sram_area_t area, permc_world_t world, permc_flags_t flags)
{
}

static inline uint32_t permc_ll_iram_get_int_source_num()
{
    return 0;
}

static inline void permc_ll_iram_enable_int()
{
}

static inline void permc_ll_iram_clear_int()
{
}

static inline void permc_ll_iram_disable_int()
{
}

static inline uint32_t permc_ll_iram_get_int_status()
{
    return 0;
}

static inline uint32_t permc_ll_iram_get_fault_wr()
{
    return 0;
}

static inline uint32_t permc_ll_iram_get_fault_loadstore()
{
    return 0;
}

static inline uint32_t permc_ll_iram_get_fault_world()
{
    return 0;
}

static inline uint32_t permc_ll_iram_get_fault_addr()
{
    return 0;
}

/*
 *  DRAM
 */
static inline void permc_ll_drom_set_perm(permc_world_t world, permc_flags_t flags)
{
}

static inline void permc_ll_dram_set_split_line(permc_split_line_t line, intptr_t addr)
{
}

static inline void permc_ll_dram_set_perm(permc_sram_area_t area, permc_world_t world, permc_flags_t flags)
{
}

static inline uint32_t permc_ll_dram_get_int_source_num()
{
    return 0;
}

static inline void permc_ll_dram_enable_int()
{
}

static inline void permc_ll_dram_clear_int()
{
}

static inline void permc_ll_dram_disable_int()
{
}

static inline uint32_t permc_ll_dram_get_int_status()
{
    return 0;
}

static inline uint32_t permc_ll_dram_get_fault_wr()
{
    return 0;
}

static inline uint32_t permc_ll_dram_get_fault_world()
{
    return 0;
}

static inline uint32_t permc_ll_dram_get_fault_addr()
{
    return 0;
}

/*
 * RTC Memory
 * - RTC memory has single split line for each WORLD
 * - Only AREA0 and AREA1 are possible, AREA0 -> LOW; AREA1 -> HIGH
 */
static inline void permc_ll_rtc_set_split_line(permc_world_t world, intptr_t addr)
{
}

static inline void permc_ll_rtc_set_perm(permc_sram_area_t area, permc_world_t world, permc_flags_t flags)
{
}

/*
 *  Flash Icache
 */
static inline void permc_ll_flash_icache_set_split_line(permc_split_line_t line, intptr_t addr)
{
}

static inline void permc_ll_flash_icache_set_perm(permc_sram_area_t area, permc_world_t world, permc_flags_t flags)
{
}

static inline uint32_t permc_ll_flash_cache_get_int_source_num()
{
    return 0;
}

static inline void permc_ll_flash_icache_enable_int()
{
}

static inline void permc_ll_flash_icache_clear_int()
{
}

static inline void permc_ll_flash_icache_disable_int()
{
}

static inline uint32_t permc_ll_flash_icache_get_int_status()
{
    return 0;
}

static inline uint32_t permc_ll_flash_icache_get_fault_attribute()
{
    return 0;
}

static inline uint32_t permc_ll_flash_icache_get_fault_world()
{
    return 0;
}

static inline uint32_t permc_ll_flash_icache_get_fault_addr()
{
    return 0;
}

/*
 * Flash Dcache
 */
static inline void permc_ll_flash_dcache_set_split_line(permc_split_line_t line, intptr_t addr)
{
}

static inline void permc_ll_flash_dcache_set_perm(permc_sram_area_t area, permc_world_t world, permc_flags_t flags)
{
}

static inline void permc_ll_flash_dcache_enable_int()
{
}

static inline void permc_ll_flash_dcache_clear_int()
{
}

static inline void permc_ll_flash_dcache_disable_int()
{
}

static inline uint32_t permc_ll_flash_dcache_get_int_status()
{
    return 0;
}

static inline uint32_t permc_ll_flash_dcache_get_fault_attribute()
{
    return 0;
}

static inline uint32_t permc_ll_flash_dcache_get_fault_world()
{
    return 0;
}

static inline uint32_t permc_ll_flash_dcache_get_fault_addr()
{
    return 0;
}

/*
 *  PIF
 */
static inline void permc_ll_pif_set_perm(permc_pif_t type, permc_world_t world, permc_flags_t flags)
{
}

static inline uint32_t permc_ll_pif_get_int_source_num()
{
    return 0;
}

static inline void permc_ll_pif_enable_int()
{
}

static inline void permc_ll_pif_clear_int()
{
}

static inline void permc_ll_pif_disable_int()
{
}

static inline uint32_t permc_ll_pif_get_int_status()
{
    return 0;
}

static inline uint32_t permc_ll_pif_get_fault_wr()
{
    return 0;
}

static inline uint32_t permc_ll_pif_get_fault_loadstore()
{
    return 0;
}

static inline uint32_t permc_ll_pif_get_fault_world()
{
    return 0;
}

static inline uint32_t permc_ll_pif_get_fault_addr()
{
    return 0;
}

#ifdef __cplusplus
}
#endif
