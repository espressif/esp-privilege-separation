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

#include <esp_rmaker_core.h>
#include <iot_button.h>

typedef enum {
    ESP_SYSCALL_EVENT_RMAKER,
    ESP_SYSCALL_EVENT_BUTTON,
} usr_rmaker_event_t;

typedef struct {
    const esp_rmaker_device_t *device;
    const esp_rmaker_param_t *param;
    const esp_rmaker_param_val_t val;
    void *priv_data;
    esp_rmaker_write_ctx_t ctx;
} usr_rmaker_write_cb_args_t;

typedef struct {
    button_cb usr_cb;
    void *usr_args;
} usr_button_cb_args_t;

typedef union {
    usr_rmaker_write_cb_args_t write_cb_args;
    usr_button_cb_args_t button_cb_args;
} usr_rmaker_dispatch_data_t;

typedef struct {
    usr_rmaker_event_t event;
    usr_rmaker_dispatch_data_t dispatch_data;
} usr_rmaker_dispatch_ctx_t;


