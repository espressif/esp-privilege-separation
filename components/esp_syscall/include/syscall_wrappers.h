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

/* This header file only consists of functions that have different prototype from their IDF
 * equivalent functions.
 * The APIs marked `custom` in syscall.tbl must be declared here to allow compilation
 *
 * APIs that are marked `common` have the same prototype and the translation is done
 * in the build system.
 */
#include "soc_defs.h"
#include "hal/gpio_types.h"
#include "driver/gpio.h"

#include "esp_event.h"

typedef void* usr_esp_event_handler_instance_t;
typedef void* usr_gpio_handle_t;

typedef enum {
    WIFI_EVENT_BASE = 0,
    IP_EVENT_BASE,
} usr_esp_event_base_t;

typedef void (*usr_esp_event_handler_t)(void* event_handler_arg,
                                        usr_esp_event_base_t event_base,
                                        int32_t event_id,
                                        void* event_data);

int usr_printf(const char *fmt, ...);

esp_err_t usr_esp_event_handler_instance_register(usr_esp_event_base_t event_base,
                                              int32_t event_id,
                                              usr_esp_event_handler_t event_handler,
                                              void *event_handler_arg,
                                              usr_esp_event_handler_instance_t *context);

esp_err_t usr_esp_event_handler_instance_unregister(usr_esp_event_base_t event_base,
                                                    int32_t event_id,
                                                    usr_esp_event_handler_instance_t context);

esp_err_t usr_gpio_isr_handler_add(gpio_num_t gpio_num, gpio_isr_t isr_handler, void *args, usr_gpio_handle_t *gpio_handle);

esp_err_t usr_gpio_isr_handler_remove(usr_gpio_handle_t gpio_handle);
