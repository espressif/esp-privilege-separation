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
}

static inline uint32_t wcntl_ll_get_mtvec_base()
{
    return 0;

}

static inline void wcntl_ll_set_entry_check(uint32_t val)
{
}

static inline uint32_t wcntl_ll_get_entry_check(uint32_t val)
{
    return 0;
}

static inline void wcntl_ll_set_w1_entry(intptr_t entry)
{
}

static inline uint32_t wcntl_ll_get_statustable_entry(uint8_t index)
{
    return 0;
}

static inline uint8_t wcntl_ll_get_current_statustable()
{
    return 0;
}

static inline void wcntl_ll_set_mstatus_mie(bool state)
{
}

#ifdef __cplusplus
}
#endif
