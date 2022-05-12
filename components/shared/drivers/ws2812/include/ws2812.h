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

#ifdef __cplusplus
extern "C" {
#endif

#define IOCTL_NUM(type, num)      (type|num)

#define WS2812_BASE         0x1000
#define WS2812_INIT         IOCTL_NUM(WS2812_BASE, 0)
#define WS2812_DEINIT       IOCTL_NUM(WS2812_BASE, 1)

typedef struct {
    uint32_t channel;
    uint32_t gpio_num;
    uint32_t led_cnt;
} ws2812_dev_conf_t;

/**
* @brief Register WS2812 device in VFS with the given pathname
*
* @param path: Path with which the device should be addressed
*
* @return:
*   - ESP_OK: Device registered successfully
*   - otherwise see esp_err_t
*/
esp_err_t esp_ws2812_device_register(const char* path);

#ifdef __cplusplus
}
#endif

