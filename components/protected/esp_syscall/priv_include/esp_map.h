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

#define ESP_MAP_QUEUE_ID        0xF5A1
#define ESP_MAP_TASK_ID         0xF5A2
#define ESP_MAP_ESP_TIMER_ID    0xF5A3
#define ESP_MAP_XTIMER_ID       0xF5A4
#define ESP_MAP_EVENT_GROUP_ID  0xF5A5
#define ESP_MAP_ESP_NETIF_ID    0xF5A6
#define ESP_MAP_GPIO_ID         0xF5A7

// Uncomment to enable CRC verification in esp_map layer
//#define ESP_MAP_ENABLE_CRC  1

#define ESP_MAP_INDEX_OFFSET 1024
#define ESP_MAP_GET_RAW_HANDLE(x) ((x)->handle)
#define ESP_MAP_GET_ID(x) ((x)->id)

typedef struct {
    uint16_t id;    // Handle type identifier
    void *handle;   // Handle returned by protected APIs
#ifdef ESP_MAP_ENABLE_CRC
    uint16_t crc;   // CRC of identifier and protected handle
#endif
} esp_map_handle_t;

void esp_map_init(void);
int esp_map_add(void *handle, int type);
esp_map_handle_t *esp_map_verify(int wrapper_index, int type);
void esp_map_remove(int wrapper_index);
esp_map_handle_t *esp_map_get_handle(int wrapper_index);
int esp_map_get_allocated_size(void);

#ifdef __cplusplus
}
#endif
