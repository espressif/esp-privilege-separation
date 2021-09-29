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

#pragma once

/* WORLD1 range */
#define SOC_UDRAM_LOW    0x3FCC0000
#define SOC_UDRAM_HIGH   0x3FCDFFFF

#define SOC_UIRAM_LOW    0x40390000
#define SOC_UIRAM_HIGH   0x4039FFFF

#define SOC_UDROM_LOW    0x3C400000
#define SOC_UDROM_HIGH   0x3C800000

#define SOC_UIROM_LOW    0x42400000
#define SOC_UIROM_HIGH   0x42800000

/* TLS system calls require larger kernel stack size.
 * If TLS not in use, this can be set to 2560
 */
#define KERNEL_STACK_SIZE   3072
#define tskSTACK_FILL_BYTE  0xa5U

#define UIRAM_ATTR __attribute__((section(".uiram")))
#define UDRAM_ATTR __attribute__((section(".udram")))

#ifndef __ASSEMBLER__

static inline int is_valid_uiram_addr(void *ptr)
{
    if (ptr >= (void *)SOC_UIRAM_LOW && ptr < (void *)SOC_UIRAM_HIGH) {
        return 1;
    }

    return 0;
}

static inline int is_valid_udram_addr(void *ptr)
{
    if (ptr >= (void *)SOC_UDRAM_LOW && ptr < (void *)SOC_UDRAM_HIGH) {
        return 1;
    }

    return 0;
}

static inline int is_valid_user_i_addr(void *ptr)
{
    if (ptr >= (void *)SOC_UIROM_LOW && ptr < (void *)SOC_UIROM_HIGH) {
        return 1;
    }

    if (ptr >= (void *)SOC_UIRAM_LOW && ptr < (void *)SOC_UIRAM_HIGH) {
        return 1;
    }

    return 0;
}

static inline int is_valid_user_d_addr(void *ptr)
{
    if (ptr >= (void *)SOC_UDROM_LOW && ptr < (void *)SOC_UDROM_HIGH) {
        return 1;
    }

    if (ptr >= (void *)SOC_UDRAM_LOW && ptr < (void *)SOC_UDRAM_HIGH) {
        return 1;
    }

    return 0;
}

static inline int is_valid_kernel_i_addr(void *ptr)
{
    if (is_valid_user_i_addr(ptr)) {
        return 0;
    }

    if (ptr >= (void *)SOC_IROM_LOW && ptr < (void *)SOC_IROM_HIGH) {
        return 1;
    }

    if (ptr >= (void *)SOC_IRAM_LOW && ptr < (void *)SOC_IRAM_HIGH) {
        return 1;
    }

    return 0;
}

static inline int is_valid_kernel_d_addr(void *ptr)
{
    if (is_valid_user_d_addr(ptr)) {
        return 0;
    }

    if (ptr >= (void *)SOC_DROM_LOW && ptr < (void *)SOC_DROM_HIGH) {
        return 1;
    }

    if (ptr >= (void *)SOC_DRAM_LOW && ptr < (void *)SOC_DRAM_HIGH) {
        return 1;
    }

    return 0;
}

#endif
