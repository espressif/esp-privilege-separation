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

static inline void wcntl_ll_set_mtvec_base(uint32_t vecbase)
{
    REG_WRITE(WORLD_CONTROL_CORE_X_MTVEC_BASE(0), vecbase);
}

static inline uint32_t wcntl_ll_get_mtvec_base()
{
    return REG_READ(WORLD_CONTROL_CORE_X_MTVEC_BASE(0));
}

static inline void wcntl_ll_set_entry_check(uint32_t val)
{
    REG_WRITE(WORLD_CONTROL_CORE_X_ENTRY_CHECK(0), val);
}

static inline void wcntl_ll_get_entry_check(uint32_t val)
{
    REG_WRITE(WORLD_CONTROL_CORE_X_ENTRY_CHECK(0), val);
}

static inline void wcntl_ll_set_w1_entry(intptr_t entry)
{
    REG_WRITE(WORLD_CONTROL_CORE_X_WORLD_PREPARE(0), 1<<1);
    REG_WRITE(WORLD_CONTROL_CORE_X_WORLD_TRIGGER_ADDR(0), entry);
    REG_WRITE(WORLD_CONTROL_CORE_X_WORLD_UPDATE(0), 1);
}

static inline uint32_t wcntl_ll_get_statustable_entry(uint8_t index)
{
    if (index > 31) {
        return 0;
    }
    return REG_READ(WORLD_CONTROL_CORE_X_STATUSTABLE(0, index));
}

static inline uint8_t wcntl_ll_get_current_statustable()
{
    return REG_READ(WORLD_CONTROL_CORE_X_STATUSTABLE_CURRENT(0));
}

static inline void wcntl_ll_set_mstatus_mie(bool state)
{
    REG_WRITE(WORLD_CONTROL_CORE_X_MSTATUS_MIE(0), state);
}

#ifdef __cplusplus
}
#endif
