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

typedef struct {
    esp_rmaker_device_write_cb_t app_write_cb;
    esp_rmaker_device_read_cb_t app_read_cb;
} rmaker_dev_priv_data_t;

esp_err_t usr_esp_rmaker_param_get_name(const esp_rmaker_param_t *param, char *buffer, int buffer_len);
esp_err_t usr_esp_rmaker_device_get_name(const esp_rmaker_device_t *device, char *buffer, int buffer_len);
esp_err_t usr_esp_rmaker_kernel_init(void);
