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
    memprot_ll_set_iram0_split_line_main_I_D((const void *)addr);
}

/*
 *  IRAM
 */
static inline void permc_ll_irom_set_perm(permc_world_t world, permc_flags_t flags)
{
    switch(world) {
        case PERMC_WORLD_0:
            REG_SET_FIELD(SENSITIVE_CORE_X_IRAM0_PMS_CONSTRAIN_2_REG, SENSITIVE_CORE_X_IRAM0_PMS_CONSTRAIN_ROM_WORLD_0_PMS, flags);
            break;
        case PERMC_WORLD_1:
            REG_SET_FIELD(SENSITIVE_CORE_X_IRAM0_PMS_CONSTRAIN_1_REG, SENSITIVE_CORE_X_IRAM0_PMS_CONSTRAIN_ROM_WORLD_1_PMS, flags);
            break;
        default:
            assert(0);
    }
}

static inline void permc_ll_icache_set_perm(permc_world_t world, permc_flags_t flags)
{
    switch(world) {
        case PERMC_WORLD_0:
            REG_SET_FIELD(SENSITIVE_CORE_X_IRAM0_PMS_CONSTRAIN_2_REG, SENSITIVE_CORE_X_IRAM0_PMS_CONSTRAIN_SRAM_WORLD_0_CACHEDATAARRAY_PMS_0, flags);
            break;
        case PERMC_WORLD_1:
            REG_SET_FIELD(SENSITIVE_CORE_X_IRAM0_PMS_CONSTRAIN_1_REG, SENSITIVE_CORE_X_IRAM0_PMS_CONSTRAIN_SRAM_WORLD_1_CACHEDATAARRAY_PMS_0, flags);
            break;
        default:
            assert(0);
    }
}

static inline void permc_ll_iram_set_split_line(permc_split_line_t line, intptr_t addr)
{
    switch(line) {
        case PERMC_SPLIT_LINE_0:
            memprot_ll_set_iram0_split_line_I_0((const void *)addr);
            break;
        case PERMC_SPLIT_LINE_1:
            memprot_ll_set_iram0_split_line_I_1((const void *)addr);
            break;
        default:
            assert(0);
    }
}

static inline void permc_ll_iram_set_perm(permc_sram_area_t area, permc_world_t world, permc_flags_t flags)
{
    /* Only WORLD0 and WORLD1 are supported */
    assert(world <= PERMC_WORLD_1);

    assert(area < PERMC_AREA_INVALID);

    uint32_t reg;
    uint32_t offset;

    if (world == PERMC_WORLD_0) {
        reg = SENSITIVE_CORE_X_IRAM0_PMS_CONSTRAIN_2_REG;
        offset = IRAM_PMS_W0_BASE;
    } else {
        reg = SENSITIVE_CORE_X_IRAM0_PMS_CONSTRAIN_1_REG;
        offset = IRAM_PMS_W1_BASE;
    }

    uint32_t shift = offset + (area * IRAM_PMS_S);

    uint32_t mask = ~(IRAM_PMS_V << shift);

    uint32_t val = flags & IRAM_PMS_V;

    REG_WRITE(reg, (REG_READ(reg) & mask) | (val << shift));
}

static inline uint32_t permc_ll_iram_get_int_source_num()
{
    return ETS_CORE0_IRAM0_PMS_INTR_SOURCE;
}

static inline void permc_ll_iram_enable_int()
{
    REG_SET_FIELD(SENSITIVE_CORE_0_IRAM0_PMS_MONITOR_1_REG, SENSITIVE_CORE_0_IRAM0_PMS_MONITOR_VIOLATE_CLR, 0);
    REG_SET_FIELD(SENSITIVE_CORE_0_IRAM0_PMS_MONITOR_1_REG, SENSITIVE_CORE_0_IRAM0_PMS_MONITOR_VIOLATE_EN, 1);
}

static inline void permc_ll_iram_clear_int()
{
    REG_SET_FIELD(SENSITIVE_CORE_0_IRAM0_PMS_MONITOR_1_REG, SENSITIVE_CORE_0_IRAM0_PMS_MONITOR_VIOLATE_CLR, 1);
}

static inline void permc_ll_iram_disable_int()
{
    REG_SET_FIELD(SENSITIVE_CORE_0_IRAM0_PMS_MONITOR_1_REG, SENSITIVE_CORE_0_IRAM0_PMS_MONITOR_VIOLATE_EN, 0);
}

static inline uint32_t permc_ll_iram_get_int_status()
{
    return memprot_ll_iram0_get_monitor_status_intr();
}

static inline uint32_t permc_ll_iram_get_fault_wr()
{
    return memprot_ll_iram0_get_monitor_status_fault_wr();
}

static inline uint32_t permc_ll_iram_get_fault_loadstore()
{
    return memprot_ll_iram0_get_monitor_status_fault_loadstore();
}

static inline uint32_t permc_ll_iram_get_fault_world()
{
    return memprot_ll_iram0_get_monitor_status_fault_world();
}

static inline uint32_t permc_ll_iram_get_fault_addr()
{
    return memprot_ll_iram0_get_monitor_status_fault_addr();
}

/*
 *  DRAM
 */
static inline void permc_ll_drom_set_perm(permc_world_t world, permc_flags_t flags)
{
    switch (world) {
        case PERMC_WORLD_0:
            REG_SET_FIELD(SENSITIVE_CORE_X_DRAM0_PMS_CONSTRAIN_1_REG, SENSITIVE_CORE_X_DRAM0_PMS_CONSTRAIN_ROM_WORLD_0_PMS, flags);
            break;
        case PERMC_WORLD_1:
            REG_SET_FIELD(SENSITIVE_CORE_X_DRAM0_PMS_CONSTRAIN_1_REG, SENSITIVE_CORE_X_DRAM0_PMS_CONSTRAIN_ROM_WORLD_1_PMS, flags);
            break;
        default:
            assert(0);
    }
}

static inline void permc_ll_dram_set_split_line(permc_split_line_t line, intptr_t addr)
{
    switch(line) {
        case PERMC_SPLIT_LINE_0:
            memprot_ll_set_dram0_split_line_D_0((const void *)addr);
            break;
        case PERMC_SPLIT_LINE_1:
            memprot_ll_set_dram0_split_line_D_1((const void *)addr);
            break;
        default:
            assert(0);
    }
}

static inline void permc_ll_dram_set_perm(permc_sram_area_t area, permc_world_t world, permc_flags_t flags)
{
    /* Only WORLD0 and WORLD1 are supported */
    assert(world <= PERMC_WORLD_1);

    assert(area < PERMC_AREA_INVALID);

    uint32_t reg = SENSITIVE_CORE_X_DRAM0_PMS_CONSTRAIN_1_REG;
    uint32_t offset;

    if (world == PERMC_WORLD_0) {
        offset = DRAM_PMS_W0_BASE;
    } else {
        offset = DRAM_PMS_W1_BASE;
    }

    uint32_t shift = offset + (area * DRAM_PMS_S);

    uint32_t mask = ~(DRAM_PMS_V << shift);

    uint32_t val = flags & DRAM_PMS_V;

    REG_WRITE(reg, (REG_READ(reg) & mask) | (val << shift));
}

static inline uint32_t permc_ll_dram_get_int_source_num()
{
    return ETS_CORE0_DRAM0_PMS_INTR_SOURCE;
}

static inline void permc_ll_dram_enable_int()
{
    REG_SET_FIELD(SENSITIVE_CORE_0_DRAM0_PMS_MONITOR_1_REG, SENSITIVE_CORE_0_DRAM0_PMS_MONITOR_VIOLATE_CLR, 0);
    REG_SET_FIELD(SENSITIVE_CORE_0_DRAM0_PMS_MONITOR_1_REG, SENSITIVE_CORE_0_DRAM0_PMS_MONITOR_VIOLATE_EN, 1);
}

static inline void permc_ll_dram_clear_int()
{
    REG_SET_FIELD(SENSITIVE_CORE_0_DRAM0_PMS_MONITOR_1_REG, SENSITIVE_CORE_0_DRAM0_PMS_MONITOR_VIOLATE_CLR, 1);
}

static inline void permc_ll_dram_disable_int()
{
    REG_SET_FIELD(SENSITIVE_CORE_0_DRAM0_PMS_MONITOR_1_REG, SENSITIVE_CORE_0_DRAM0_PMS_MONITOR_VIOLATE_EN, 0);
}

static inline uint32_t permc_ll_dram_get_int_status()
{
    return memprot_ll_dram0_get_monitor_status_intr();
}

static inline uint32_t permc_ll_dram_get_fault_wr()
{
    return memprot_ll_dram0_get_monitor_status_fault_wr();
}

static inline uint32_t permc_ll_dram_get_fault_world()
{
    return memprot_ll_dram0_get_monitor_status_fault_world();
}

static inline uint32_t permc_ll_dram_get_fault_addr()
{
    return memprot_ll_dram0_get_monitor_status_fault_addr();
}

/*
 * RTC Memory
 * - RTC memory has single split line for each WORLD
 * - Only AREA0 and AREA1 are possible, AREA0 -> LOW; AREA1 -> HIGH
 */
static inline void permc_ll_rtc_set_split_line(permc_world_t world, intptr_t addr)
{
    switch (world) {
        case PERMC_WORLD_0:
            REG_SET_FIELD(SENSITIVE_CORE_0_PIF_PMS_CONSTRAIN_9_REG, SENSITIVE_CORE_0_PIF_PMS_CONSTRAIN_RTCFAST_SPLTADDR_WORLD_0, (addr >> 2));
            break;
        case PERMC_WORLD_1:
            REG_SET_FIELD(SENSITIVE_CORE_0_PIF_PMS_CONSTRAIN_9_REG, SENSITIVE_CORE_0_PIF_PMS_CONSTRAIN_RTCFAST_SPLTADDR_WORLD_1, (addr >> 2));
            break;
        default:
            assert(0);
    }
}

static inline void permc_ll_rtc_set_perm(permc_sram_area_t area, permc_world_t world, permc_flags_t flags)
{
    assert(world <= PERMC_WORLD_1);

    /* Since there is a single split line, only 2 areas are possible */
    assert(area < PERMC_AREA_2);

    uint32_t reg = SENSITIVE_CORE_0_PIF_PMS_CONSTRAIN_10_REG;
    uint32_t offset;
    uint32_t perm = 0;

    /* For RTC, the order for R and W permission is reversed
     * 1: Write
     * 2: Read
     * 4: Execute
     */
    if (flags & PERMC_ACCESS_R) {
        perm = perm | 2;
    }

    if (flags & PERMC_ACCESS_W) {
        perm = perm | 1;

    }

    if (flags & PERMC_ACCESS_X) {
        perm = perm | 4;
    }

    if (world == PERMC_WORLD_0) {
        offset = RTC_PMS_W0_BASE;
    } else {
        offset = RTC_PMS_W1_BASE;
    }

    uint32_t shift = offset + (area * RTC_PMS_S);

    uint32_t mask = ~(RTC_PMS_V << shift);

    uint32_t val = flags & RTC_PMS_V;

    REG_WRITE(reg, (REG_READ(reg) & mask) | (val << shift));
}

/*
 *  Flash Icache
 */
static inline void permc_ll_flash_icache_set_split_line(permc_split_line_t line, intptr_t addr)
{
    switch(line) {
        case PERMC_SPLIT_LINE_0:
            REG_SET_FIELD(EXTMEM_IBUS_PMS_TBL_BOUNDARY0_REG, EXTMEM_IBUS_PMS_BOUNDARY0, (addr >> 12));
            break;
        case PERMC_SPLIT_LINE_1:
            REG_SET_FIELD(EXTMEM_IBUS_PMS_TBL_BOUNDARY1_REG, EXTMEM_IBUS_PMS_BOUNDARY1, (addr >> 12));
            break;
        case PERMC_SPLIT_LINE_2:
            REG_SET_FIELD(EXTMEM_IBUS_PMS_TBL_BOUNDARY2_REG, EXTMEM_IBUS_PMS_BOUNDARY2, (addr >> 12));
            break;
        default:
            assert(0);
    }
}

static inline void permc_ll_flash_icache_set_perm(permc_sram_area_t area, permc_world_t world, permc_flags_t flags)
{
    uint32_t shift = (2 * world);

    if (flags == PERMC_ACCESS_ALL) {
        flags = 0b11;
    } else if (flags & PERMC_ACCESS_W) {
        assert(0);
    } else if (flags & PERMC_ACCESS_X) {
        flags = flags | 0b10;
    }

    uint32_t mask = ~(FLASH_ICACHE_V << shift);

    uint32_t val = flags << shift;

    switch (area) {
        case PERMC_AREA_0: {
            uint32_t reg_val = REG_GET_FIELD(EXTMEM_IBUS_PMS_TBL_ATTR_REG, EXTMEM_IBUS_PMS_SCT1_ATTR);
            REG_SET_FIELD(EXTMEM_IBUS_PMS_TBL_ATTR_REG, EXTMEM_IBUS_PMS_SCT1_ATTR, (reg_val & mask) | val);
            break;
        }
        case PERMC_AREA_1: {
            uint32_t reg_val = REG_GET_FIELD(EXTMEM_IBUS_PMS_TBL_ATTR_REG, EXTMEM_IBUS_PMS_SCT2_ATTR);
            REG_SET_FIELD(EXTMEM_IBUS_PMS_TBL_ATTR_REG, EXTMEM_IBUS_PMS_SCT2_ATTR, (reg_val & mask) | val);
            break;
        }
        default:
            assert(0);
    }
}

static inline uint32_t permc_ll_flash_cache_get_int_source_num()
{
    return ETS_CACHE_CORE0_ACS_INTR_SOURCE;
}

static inline void permc_ll_flash_icache_enable_int()
{
    REG_SET_FIELD(EXTMEM_CORE0_ACS_CACHE_INT_CLR_REG, EXTMEM_CORE0_IBUS_REJECT_INT_CLR, 0);
	REG_SET_FIELD(EXTMEM_CORE0_ACS_CACHE_INT_ENA_REG, EXTMEM_CORE0_IBUS_REJECT_INT_ENA, 1);
}

static inline void permc_ll_flash_icache_clear_int()
{
    REG_SET_FIELD(EXTMEM_CORE0_ACS_CACHE_INT_CLR_REG, EXTMEM_CORE0_IBUS_REJECT_INT_CLR, 1);
}

static inline void permc_ll_flash_icache_disable_int()
{
	REG_SET_FIELD(EXTMEM_CORE0_ACS_CACHE_INT_ENA_REG, EXTMEM_CORE0_IBUS_REJECT_INT_ENA, 0);
}

static inline uint32_t permc_ll_flash_icache_get_int_status()
{
    return REG_GET_FIELD(EXTMEM_CORE0_ACS_CACHE_INT_ST_REG, EXTMEM_CORE0_IBUS_REJECT_ST);
}

static inline uint32_t permc_ll_flash_icache_get_fault_attribute()
{
    /* 1 : Execute
     * 2 : Read
     */
    return REG_GET_FIELD(EXTMEM_CORE0_IBUS_REJECT_ST_REG, EXTMEM_CORE0_IBUS_ATTR);
}

static inline uint32_t permc_ll_flash_icache_get_fault_world()
{
    return REG_GET_FIELD(EXTMEM_CORE0_IBUS_REJECT_ST_REG, EXTMEM_CORE0_IBUS_WORLD);
}

static inline uint32_t permc_ll_flash_icache_get_fault_addr()
{
    return REG_READ(EXTMEM_CORE0_IBUS_REJECT_VADDR_REG);
}

/*
 * Flash Dcache
 */
static inline void permc_ll_flash_dcache_set_split_line(permc_split_line_t line, intptr_t addr)
{
    switch(line) {
        case PERMC_SPLIT_LINE_0:
            REG_SET_FIELD(EXTMEM_DBUS_PMS_TBL_BOUNDARY0_REG, EXTMEM_DBUS_PMS_BOUNDARY0, (addr >> 12));
            break;
        case PERMC_SPLIT_LINE_1:
            REG_SET_FIELD(EXTMEM_DBUS_PMS_TBL_BOUNDARY1_REG, EXTMEM_DBUS_PMS_BOUNDARY1, (addr >> 12));
            break;
        case PERMC_SPLIT_LINE_2:
            REG_SET_FIELD(EXTMEM_DBUS_PMS_TBL_BOUNDARY2_REG, EXTMEM_DBUS_PMS_BOUNDARY2, (addr >> 12));
            break;
        default:
            assert(0);
    }
}

static inline void permc_ll_flash_dcache_set_perm(permc_sram_area_t area, permc_world_t world, permc_flags_t flags)
{
    uint32_t shift = (1 * world);

    if (flags == PERMC_ACCESS_ALL) {
        flags = 1;
    }

    if (flags > PERMC_ACCESS_R) {
        assert(0);
    };

    uint32_t mask = ~(FLASH_DCACHE_V << shift);

    uint32_t val = flags << shift;

    switch (area) {
        case PERMC_AREA_0: {
            uint32_t reg_val = REG_GET_FIELD(EXTMEM_DBUS_PMS_TBL_ATTR_REG, EXTMEM_DBUS_PMS_SCT1_ATTR);
            REG_SET_FIELD(EXTMEM_DBUS_PMS_TBL_ATTR_REG, EXTMEM_DBUS_PMS_SCT1_ATTR, (reg_val & mask) | val);
            break;
        }
        case PERMC_AREA_1: {
            uint32_t reg_val = REG_GET_FIELD(EXTMEM_DBUS_PMS_TBL_ATTR_REG, EXTMEM_DBUS_PMS_SCT2_ATTR);
            REG_SET_FIELD(EXTMEM_DBUS_PMS_TBL_ATTR_REG, EXTMEM_DBUS_PMS_SCT2_ATTR, (reg_val & mask) | val);
            break;
        }
        default:
            assert(0);
    }
}

static inline void permc_ll_flash_dcache_enable_int()
{
	REG_SET_FIELD(EXTMEM_CORE0_ACS_CACHE_INT_CLR_REG, EXTMEM_CORE0_DBUS_REJECT_INT_CLR, 0);
	REG_SET_FIELD(EXTMEM_CORE0_ACS_CACHE_INT_ENA_REG, EXTMEM_CORE0_DBUS_REJECT_INT_ENA, 1);
}

static inline void permc_ll_flash_dcache_clear_int()
{
    REG_SET_FIELD(EXTMEM_CORE0_ACS_CACHE_INT_CLR_REG, EXTMEM_CORE0_DBUS_REJECT_INT_CLR, 1);
}

static inline void permc_ll_flash_dcache_disable_int()
{
    REG_SET_FIELD(EXTMEM_CORE0_ACS_CACHE_INT_ENA_REG, EXTMEM_CORE0_DBUS_REJECT_INT_ENA, 0);
}

static inline uint32_t permc_ll_flash_dcache_get_int_status()
{
    return REG_GET_FIELD(EXTMEM_CORE0_ACS_CACHE_INT_ST_REG, EXTMEM_CORE0_DBUS_REJECT_ST);
}

static inline uint32_t permc_ll_flash_dcache_get_fault_attribute()
{
    return REG_GET_FIELD(EXTMEM_CORE0_DBUS_REJECT_ST_REG, EXTMEM_CORE0_DBUS_ATTR);
}

static inline uint32_t permc_ll_flash_dcache_get_fault_world()
{
    return REG_GET_FIELD(EXTMEM_CORE0_DBUS_REJECT_ST_REG, EXTMEM_CORE0_DBUS_WORLD);
}

static inline uint32_t permc_ll_flash_dcache_get_fault_addr()
{
    return REG_READ(EXTMEM_CORE0_DBUS_REJECT_VADDR_REG);
}

/*
 *  PIF
 */
static inline void permc_ll_pif_set_perm(permc_pif_t type, permc_world_t world, permc_flags_t flags)
{
    assert(type < PERMC_MAX);

    uint32_t reg = 0;
    uint32_t reg_off = type / PIF_PMS_MAX_REG_ENTRY;
    uint32_t bit_field_base = 30 - (2 * (type % PIF_PMS_MAX_REG_ENTRY));

    switch (world) {
        case PERMC_WORLD_0:
            reg = SENSITIVE_CORE_0_PIF_PMS_CONSTRAIN_1_REG + (4 * reg_off);
            break;
        case PERMC_WORLD_1:
            reg = SENSITIVE_CORE_0_PIF_PMS_CONSTRAIN_5_REG + (4 * reg_off);
            break;
        default:
            assert(0);
    }

    uint32_t mask = ~(PIF_PMS_V << bit_field_base);

    uint32_t val = flags & PIF_PMS_V;

    REG_WRITE(reg, (REG_READ(reg) & mask) | (val << bit_field_base));
}

static inline uint32_t permc_ll_pif_get_int_source_num()
{
    return ETS_CORE0_PIF_PMS_INTR_SOURCE;
}

static inline void permc_ll_pif_enable_int()
{
    REG_SET_FIELD(SENSITIVE_CORE_0_PIF_PMS_MONITOR_1_REG, SENSITIVE_CORE_0_PIF_PMS_MONITOR_VIOLATE_CLR, 0);
    REG_SET_FIELD(SENSITIVE_CORE_0_PIF_PMS_MONITOR_1_REG, SENSITIVE_CORE_0_PIF_PMS_MONITOR_VIOLATE_EN, 1);
}

static inline void permc_ll_pif_clear_int()
{
    REG_SET_FIELD(SENSITIVE_CORE_0_PIF_PMS_MONITOR_1_REG, SENSITIVE_CORE_0_PIF_PMS_MONITOR_VIOLATE_CLR, 1);
}

static inline void permc_ll_pif_disable_int()
{
    REG_SET_FIELD(SENSITIVE_CORE_0_PIF_PMS_MONITOR_1_REG, SENSITIVE_CORE_0_PIF_PMS_MONITOR_VIOLATE_EN, 0);
}

static inline uint32_t permc_ll_pif_get_int_status()
{
    return REG_GET_FIELD(SENSITIVE_CORE_0_PIF_PMS_MONITOR_2_REG, SENSITIVE_CORE_0_PIF_PMS_MONITOR_VIOLATE_INTR);
}

static inline uint32_t permc_ll_pif_get_fault_wr()
{
    return REG_GET_FIELD(SENSITIVE_CORE_0_PIF_PMS_MONITOR_2_REG, SENSITIVE_CORE_0_PIF_PMS_MONITOR_VIOLATE_STATUS_HWRITE);
}

static inline uint32_t permc_ll_pif_get_fault_loadstore()
{
    return REG_GET_FIELD(SENSITIVE_CORE_0_PIF_PMS_MONITOR_2_REG, SENSITIVE_CORE_0_PIF_PMS_MONITOR_VIOLATE_STATUS_HPORT_0);
}

static inline uint32_t permc_ll_pif_get_fault_world()
{
    return REG_GET_FIELD(SENSITIVE_CORE_0_PIF_PMS_MONITOR_2_REG, SENSITIVE_CORE_0_PIF_PMS_MONITOR_VIOLATE_STATUS_HWORLD);
}

static inline uint32_t permc_ll_pif_get_fault_addr()
{
    return REG_READ(SENSITIVE_CORE_0_PIF_PMS_MONITOR_3_REG);
}

#ifdef __cplusplus
}
#endif
