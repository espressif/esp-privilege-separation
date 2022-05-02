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

/* This header file only consists of functions that have different prototype from their IDF
 * equivalent functions.
 * The APIs marked `custom` in syscall.tbl must be declared here to allow compilation
 *
 * APIs that are marked `common` have the same prototype and the translation is done
 * in the build system.
 */

#pragma once

#include "soc_defs.h"
#include "hal/gpio_types.h"
#include "driver/gpio.h"

#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void* usr_gpio_handle_t;

/**
 * @brief Register User space GPIO Soft-ISR handler
 *
 * @param gpio_num GPIO numnber to which this handler is attached
 * @param softisr_handler User space soft-ISR handler which will be invoked when an interrupt occurs on the given GPIO
 * @param args  User specified argument
 * @param gpio_handle Handle attached to this soft-isr handler
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL otherwise
 *
 * @note
 *      User code is never executed in ISR context so there can't be user registered ISR handlers,
 *      we still have a provision that allows user code to register a soft-isr handler that works
 *      like a callback and is executed in a task's context
 */
esp_err_t gpio_softisr_handler_add(gpio_num_t gpio_num, gpio_isr_t softisr_handler, void *args, usr_gpio_handle_t *gpio_handle);

/**
 * @brief Remove User space GPIO Soft-ISR handler
 *
 * @param gpio_handle Handle for the previously added soft-isr handler
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL otherwise
 */
esp_err_t gpio_softisr_handler_remove(usr_gpio_handle_t gpio_handle);

#ifdef __cplusplus
}
#endif
