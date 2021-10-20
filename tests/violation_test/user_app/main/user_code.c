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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "syscall_wrappers.h"

#include "soc/gpio_struct.h"
#include "hal/gpio_types.h"
#include "driver/gpio.h"
#include "hal/gpio_ll.h"

#define BLINK_GPIO      GPIO_NUM_4

extern uint32_t _user_data_start;
extern uint32_t _user_text_start;
extern uint32_t _user_xip_text_start;

void iram_violation_task()
{
    uint32_t start = (uint32_t)&_user_text_start;

    void (*forbidden_iram)(void) = (void(*)(void))(start - 0x1000);

    forbidden_iram();

    while(1) {
        ;
    }
}

void dram_violation_task()
{
    printf("Writing to forbidden region\n");
    uint32_t *forbidden_dram = (&_user_data_start) - 0x10000;

    *forbidden_dram = 0x41414141;

    while(1) {
        ;
    }
}

void pif_violation_task()
{
    gpio_ll_set_level(&GPIO, BLINK_GPIO, 1);

    while (1) {
        ;
    }
}

void flash_icache_violation_task()
{
    uint32_t start = (uint32_t)&_user_xip_text_start;

    void (*forbidden_flash_cache)(void) = (void(*)(void))(start - 0x10000);

    forbidden_flash_cache();

    while(1) {
        ;
    }
}

void user_main()
{
    printf("Testing violations\n");

    if (xTaskCreate(iram_violation_task, "iram_task", 2048, NULL, 5, NULL) != pdPASS) {
        printf("Task Creation failed\n");
    }

    if (xTaskCreate(dram_violation_task, "dram_task", 2048, NULL, 5, NULL) != pdPASS) {
        printf("Task Creation failed\n");
    }

    if (xTaskCreate(pif_violation_task, "pif_task", 2048, NULL, 5, NULL) != pdPASS) {
        printf("Task Creation failed\n");
    }

    if (xTaskCreate(flash_icache_violation_task, "flash_task", 2048, NULL, 5, NULL) != pdPASS) {
        printf("Task Creation failed\n");
    }
}
