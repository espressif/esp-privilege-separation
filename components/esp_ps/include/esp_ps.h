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

#include <string.h>
#include "esp_image_format.h"
#include <esp_spi_flash.h>
#include <esp_partition.h>
#include "esp_log.h"

#define PS_INTR_ATTR IRAM_ATTR __attribute__((noinline))

typedef void (*esp_ps_intr_handler_t)(void *arg);

typedef enum {
    PS_IRAM_INT = 1,
    PS_DRAM_INT,
    PS_FLASH_ICACHE_INT,
    PS_PERIPH_INT,
} esp_ps_int_t;

typedef enum {
    PS_UART1 = 0,
    PS_I2C = 2,
    PS_MISC,
    PS_WDG = 6,
    PS_IO_MUX,
    PS_RTC,
    PS_TIMER,
    PS_FE,
    PS_FE2,
    PS_GPIO,
    PS_G0SPI_0,
    PS_G0SPI_1,
    PS_UART,
    PS_SYSTIMER,
    PS_TIMERGROUP1,
    PS_TIMERGROUP,
    PS_BB = 20,
    PS_LEDC = 23,
    PS_RMT = 26,
    PS_UHCI0 = 28,
    PS_I2C_EXT0,
    PS_BT = 31,
    PS_PWR = 33,
    PS_WIFIMAC,
    PS_RWBT = 36,
    PS_I2S1 = 40,
    PS_CAN = 42,
    PS_APB_CTRL = 45,
    PS_SPI_2 = 47,
    PS_WORLD_CONTROLLER,
    PS_DIO,
    PS_AD,
    PS_CACHE_CONFIG,
    PS_DMA_COPY,
    PS_INTERRUPT,
    PS_SENSITIVE,
    PS_SYSTEM,
    PS_USB_DEVICE,
    PS_BT_PWR,
    PS_APB_ADC = 59,
    PS_CRYPTO_DMA,
    PS_CRYPTO_PERI,
    PS_USB_WRAP,
    PS_MAX,
} esp_ps_periph_t;

typedef enum {
    PS_WORLD_0 = 0,         /* Protected app */
    PS_WORLD_1              /* User app */
} esp_ps_world_t;

typedef enum {
    PS_PERM_NONE = 0,
    PS_PERM_R = 1,
    PS_PERM_W = 2,
    PS_PERM_X = 4,
    PS_PERM_ALL = 7,
} esp_ps_perm_t;

/**
 * @brief Initialize PS component
 *
 * Configures various memory regions, sets split lines and permissions.
 * Enables interrupt for permission violation and registers user specified interrupt handler
 *
 * @param fn Interrupt handler which will be invoked whenever any permission violation occurs
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL otherwise
 */
esp_err_t esp_ps_init(esp_ps_intr_handler_t fn);

/**
 * @brief Unpack, load and boot user app
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_INVALID_ARG if incorrect user entry
 *      - ESP_ERR_NO_MEM if memory exhausted
 *      - ESP_FAIL otherwise
 */
esp_err_t esp_ps_user_boot();

/**
 * @brief Set entry to user space. When the entry address is fetched, CPU switches to user space
 *
 * @param user_entry Pointer to user space code
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL if user_entry is invalid
 */
esp_err_t esp_ps_user_set_entry(void *user_entry);

/**
 * @brief Spawn a task that executes under user space
 *
 * @param user_entry Pointer to task entry function
 * @param stack_sz Size of the task stack
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL if user_entry is invalid
 */
esp_err_t esp_ps_user_spawn_task(void *user_entry, uint32_t stack_sz);

/**
 * @brief Converts interrupt type to corresponding memory region string
 *
 * @param int_type Interrupt type, see esp_ps_int_t
 *
 * @return Pointer to a string
 */
char *esp_ps_int_type_to_str(esp_ps_int_t int_type);

/**
 * @brief Enable interrupt for a given violation type
 *
 * @param int_type Interrupt type, see esp_ps_int_t
 */
void esp_ps_enable_int(esp_ps_int_t int_type);

/**
 * @brief Clear and re-enable interrupt for the given interrupt type
 *
 * @param int_type Interrupt type, see esp_ps_int_t
 */
void esp_ps_clear_and_reenable_int(esp_ps_int_t int_type);

/**
 * @brief Get the triggered violation interrupt, if any
 *
 * @return Interrupt type of triggered violation
 */
esp_ps_int_t esp_ps_get_int_status();

/**
 * @brief Get the fault address that triggered violation interrupt
 *
 * @param int_type Interrupt type, see esp_ps_int_t
 *
 * @return Address which triggered the interrupt
 */
uint32_t esp_ps_get_fault_addr(esp_ps_int_t int_type);

/**
 * @brief Set the permissions for a specified peripheral under a WORLD
 *
 * @param periph One of the supported peripheral, see esp_ps_periph_t
 * @param world WORLD under which the following permissions will be enforced
 * @param perm Permissions for the peripheral
 *
 * @return
 *      - ESP_OK on success
 */
esp_err_t esp_ps_set_periph_perm(esp_ps_periph_t periph, esp_ps_world_t world, esp_ps_perm_t perm);
