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

#include "soc/soc.h"
#include "soc/world_controller_reg.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Only configured for core 0
 * TODO: Add dual core support
 *
 */

typedef enum {
    WCNTL_WORLD_0 = 0,
    WCNTL_WORLD_1
} wcntl_world_t;

static inline void wcntl_ll_set_vecbase(wcntl_world_t world, uint32_t vecbase)
{
    REG_SET_FIELD(SENSITIVE_CORE_0_VECBASE_OVERRIDE_0_REG, SENSITIVE_CORE_0_VECBASE_WORLD_MASK, 0);
    REG_SET_FIELD(SENSITIVE_CORE_0_VECBASE_OVERRIDE_1_REG, SENSITIVE_CORE_0_VECBASE_OVERRIDE_SEL, 0b11);
    switch (world) {
        case WCNTL_WORLD_0:
            REG_SET_FIELD(SENSITIVE_CORE_0_VECBASE_OVERRIDE_1_REG, SENSITIVE_CORE_0_VECBASE_OVERRIDE_WORLD0_VALUE, (vecbase >> 10));
            break;
        case WCNTL_WORLD_1:
            REG_SET_FIELD(SENSITIVE_CORE_0_VECBASE_OVERRIDE_2_REG, SENSITIVE_CORE_0_VECBASE_OVERRIDE_WORLD1_VALUE, (vecbase >> 10));
            break;
        default:
            assert(0);
    }
}

static inline uint32_t wcntl_ll_get_vecbase(wcntl_world_t world)
{
    uint32_t vecbase;
    switch (world) {
        case WCNTL_WORLD_0:
            vecbase = REG_GET_FIELD(SENSITIVE_CORE_0_VECBASE_OVERRIDE_1_REG, SENSITIVE_CORE_0_VECBASE_OVERRIDE_WORLD0_VALUE) >> 10;
            break;
        case WCNTL_WORLD_1:
            vecbase = REG_GET_FIELD(SENSITIVE_CORE_0_VECBASE_OVERRIDE_1_REG, SENSITIVE_CORE_0_VECBASE_OVERRIDE_WORLD1_VALUE) >> 10;
            break;
        default:
            assert(0);
    }
    return vecbase;
}

static inline void wcntl_ll_set_entry_check(int index, uint32_t addr)
{
    if (index < 1 || index > 13) {
        assert(0);
    }

    REG_WRITE(WORLD_CONTROLLER_WCL_CORE_0_MESSAGE_ADDR_REG, 0x50000000);
    REG_WRITE(WORLD_CONTROLLER_WCL_CORE_0_MESSAGE_MAX_REG, 6);

    uint32_t reg = WORLD_CONTROLLER_WCL_CORE_0_ENTRY_1_ADDR_REG + ((index - 1) * 4);

    // Write ENTRY address
    REG_WRITE(reg, addr);

    // Enable check for that particular addr. When fetched, WORLD will switch to WORLD0
    REG_SET_BIT(WORLD_CONTROLLER_WCL_CORE_0_ENTRY_CHECK_REG, (1 << index));
}

static inline uint32_t wcntl_ll_get_entry_check(uint32_t val)
{
    return REG_READ(WORLD_CONTROLLER_WCL_CORE_0_ENTRY_CHECK_REG);
}

static inline void wcntl_ll_set_w1_entry(intptr_t entry)
{
    REG_WRITE(WORLD_CONTROLLER_WCL_CORE_0_WORLD_PREPARE_REG, 1<<1);
    REG_WRITE(WORLD_CONTROLLER_WCL_CORE_0_WORLD_TRIGGER_ADDR_REG, entry);
    REG_WRITE(WORLD_CONTROLLER_WCL_CORE_0_WORLD_UPDATE_REG, 1);
}

static inline uint32_t wcntl_ll_get_statustable_entry(uint8_t index)
{
    if (index < 1 || index > 13) {
        return 0;
    }
    uint32_t reg = WORLD_CONTROLLER_WCL_CORE_0_STATUSTABLE1_REG;

    return REG_READ((reg + 4 * (index - 1)));
}

static inline uint8_t wcntl_ll_get_current_statustable()
{
    return REG_READ(WORLD_CONTROLLER_WCL_CORE_0_STATUSTABLE_CURRENT_REG);
}

static inline void wcntl_ll_set_mstatus_mie(bool state)
{
    // No such register in Xtensa
}

#ifdef __cplusplus
}
#endif
