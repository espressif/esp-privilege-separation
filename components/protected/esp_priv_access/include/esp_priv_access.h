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

#include <string.h>
#include "esp_image_format.h"
#include <esp_spi_flash.h>
#include <esp_partition.h>
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*esp_priv_access_intr_handler_t)(void *arg);

typedef enum {
    PA_IRAM_INT = 1,
    PA_DRAM_INT,
    PA_RTC_INT,
    PA_FLASH_ICACHE_INT,
    PA_PERIPH_INT,
} esp_priv_access_int_t;

typedef enum {
    PA_UART1 = 0,
    PA_I2C = 2,
    PA_MISC,
    PA_WDG = 6,
    PA_IO_MUX,
    PA_RTC,
    PA_TIMER,
    PA_FE,
    PA_FE2,
    PA_GPIO,
    PA_G0SPI_0,
    PA_G0SPI_1,
    PA_UART,
    PA_SYSTIMER,
    PA_TIMERGROUP1,
    PA_TIMERGROUP,
    PA_BB = 20,
    PA_LEDC = 23,
    PA_RMT = 26,
    PA_UHCI0 = 28,
    PA_I2C_EXT0,
    PA_BT = 31,
    PA_PWR = 33,
    PA_WIFIMAC,
    PA_RWBT = 36,
    PA_I2S1 = 40,
    PA_CAN = 42,
    PA_APB_CTRL = 45,
    PA_SPI_2 = 47,
    PA_WORLD_CONTROLLER,
    PA_DIO,
    PA_AD,
    PA_CACHE_CONFIG,
    PA_DMA_COPY,
    PA_INTERRUPT,
    PA_SENSITIVE,
    PA_SYSTEM,
    PA_USB_DEVICE,
    PA_BT_PWR,
    PA_APB_ADC = 59,
    PA_CRYPTO_DMA,
    PA_CRYPTO_PERI,
    PA_USB_WRAP,
    PA_MAX,
} esp_priv_access_periph_t;

typedef enum {
    PA_WORLD_0 = 0,         /* Protected app */
    PA_WORLD_1              /* User app */
} esp_priv_access_world_t;

typedef enum {
    PA_PERM_NONE = 0,
    PA_PERM_R = 1,
    PA_PERM_W = 2,
    PA_PERM_X = 4,
    PA_PERM_ALL = 7,
} esp_priv_access_perm_t;

/**
 * @brief Initialize Privilege Separation (PA) component
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
esp_err_t esp_priv_access_init(esp_priv_access_intr_handler_t fn);

/**
 * @brief Unpack, load and boot user app
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_INVALID_ARG if incorrect user entry
 *      - ESP_ERR_NO_MEM if memory exhausted
 *      - ESP_FAIL otherwise
 */
esp_err_t esp_priv_access_user_boot();

/**
 * @brief Reboots user app.
 *        Deletes all the user tasks and then calls esp_priv_access_user_boot
 */
void esp_priv_access_user_reboot();

/**
 * @brief Set entry to user space. When the entry address is fetched, CPU switches to user space
 *
 * @param user_entry Pointer to user space code
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL if user_entry is invalid
 */
esp_err_t esp_priv_access_user_set_entry(void *user_entry);

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
esp_err_t esp_priv_access_user_spawn_task(void *user_entry, uint32_t stack_sz);

/**
 * @brief Converts interrupt type to corresponding memory region string
 *
 * @param int_type Interrupt type, see esp_priv_access_int_t
 *
 * @return Pointer to a string
 */
char *esp_priv_access_int_type_to_str(esp_priv_access_int_t int_type);

/**
 * @brief Enable interrupt for a given violation type
 *
 * @param int_type Interrupt type, see esp_priv_access_int_t
 */
void esp_priv_access_enable_int(esp_priv_access_int_t int_type);

/**
 * @brief Clear and re-enable interrupt for the given interrupt type
 *
 * @param int_type Interrupt type, see esp_priv_access_int_t
 */
void esp_priv_access_clear_and_reenable_int(esp_priv_access_int_t int_type);

/**
 * @brief Get the triggered violation interrupt, if any
 *
 * @return Interrupt type of triggered violation
 */
esp_priv_access_int_t esp_priv_access_get_int_status();

/**
 * @brief Get the fault address that triggered violation interrupt
 *
 * @param int_type Interrupt type, see esp_priv_access_int_t
 *
 * @return Address which triggered the interrupt
 */
uint32_t esp_priv_access_get_fault_addr(esp_priv_access_int_t int_type);

/**
 * @brief Set the permissions for a specified peripheral under a WORLD
 *
 * @param periph One of the supported peripheral, see esp_priv_access_periph_t
 * @param world WORLD under which the following permissions will be enforced
 * @param perm Permissions for the peripheral
 *
 * @return
 *      - ESP_OK on success
 */
esp_err_t esp_priv_access_set_periph_perm(esp_priv_access_periph_t periph, esp_priv_access_world_t world, esp_priv_access_perm_t perm);

#ifdef __cplusplus
}
#endif
