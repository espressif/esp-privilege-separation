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

#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <esp_log.h>
#include <esp_map.h>
#include <soc_defs.h>

#ifdef ESP_MAP_ENABLE_CRC
#ifdef CONFIG_IDF_TARGET_ESP32C3
#include "esp32c3/rom/crc.h"
#endif
#endif

#define TICKS_TO_WAIT       portMAX_DELAY // Ticks to wait for acquiring the mutex
#define INIT_HANDLES        64  // Number of esp_map handles supported by default
#define SIZE_OF_HANDLE (sizeof(intptr_t))

#ifdef ESP_MAP_ENABLE_CRC
#define WRAP_HANDLE_SIZE (sizeof(esp_map_handle_t) - sizeof(uint16_t))
#endif

#define ESP_MAP_GET_SHIM_HANDLE(x) ((esp_map_handle_t *)allocated_handles[(x) - ESP_MAP_INDEX_OFFSET])

static const char *TAG = "esp_map";

/*
 * esp_map is a two layered structure used to maintain user app resources allocated
 * on protected app side. First layer uses esp_map_handle_t structure, which holds
 * type of the resource and actual pointer to protected app resource. CRC verification
 * can be enabled for additional security. Second layer is a dynamically allocated
 * array holding pointer to esp_map_handle_t. Protected app does not return kernel
 * DRAM pointer to user app and instead return the index of esp_map_handle_t structure
 * stored in the array. esp_map can also be implemented as a single layered structure
 * by allocating array of the type esp_map_handle_t. However, this will add into the
 * complexity of searching for free slots in the array. Hence, it is broken into two
 * layers. esp_map layer uses a mutex for thread safety and this layer cannot be used
 * from an ISR or a critical section.
 *
 * The following usecase demonstrates esp_map implementation:
 *
 * |-------------------------------------------------------------|
 * | Actual Resource |    esp_map_handle_t    |     Map Index    |
 * |-------------------------------------------------------------|
 * |    Semaphore    | id = ESP_MAP_QUEUE_ID  |   0x3FCC6000     |
 * |                 | handle = 0x3FC96100    |                  |
 * |   (0x3FC96100)  |    (0x3FCC6000)        |      (1024)      |
 * |-------------------------------------------------------------|
 * |      Task       | id = ESP_MAP_TASK_ID   |    0x3FCC6004    |
 * |                 | handle = 0x3FC96104    |                  |
 * |   (0x3FC96104)  |    (0x3FCC6004)        |     (1025)       |
 * |-------------------------------------------------------------|
 * |     xTimer      | id = ESP_MAP_XTIMER_ID |    0x3FCC6008    |
 * |                 | handle = 0x3FC96104    |                  |
 * |   (0x3FC96104)  |    (0x3FCC6008)        |     (1026)       |
 * |-------------------------------------------------------------|
 */

static DRAM_ATTR intptr_t *allocated_handles;   // Array of handles created by esp_map layer
static DRAM_ATTR int allocated_handle_size;     // Size of currently allocated array
static DRAM_ATTR SemaphoreHandle_t map_lock;    // Mutex for adding thread safety in esp_map layer

_Static_assert(sizeof(allocated_handles) == SIZE_OF_HANDLE, "Size of allocated_handles array does not match size of intptr_t.");

void __attribute__((constructor)) esp_map_init(void)
{
    map_lock = xSemaphoreCreateMutex();
    if (!map_lock) {
        ESP_EARLY_LOGE(TAG, "Mutex for esp_map could not be created");
    }
    allocated_handles = (intptr_t *)heap_caps_calloc(1, INIT_HANDLES * SIZE_OF_HANDLE, MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL);
    if (!allocated_handles) {
        ESP_LOGE(TAG, "Failed to allocate array for handles");
        abort();
    }
    allocated_handle_size = INIT_HANDLES;
}

static inline int _esp_map_get_free_index(void)
{
    for (int i = 0; i < allocated_handle_size; i++) {
        if (!allocated_handles[i]) {
            return i;
        }
    }
    return -1;
}

/* Allocate memory for handle wrapper and set its members */
int esp_map_add(void *handle, int type)
{
    /* esp_map APIs should not be called from ISR context. Hence, this assert is added */
    assert(xPortCanYield());
    esp_map_handle_t *wrapper_handle = heap_caps_malloc(sizeof(esp_map_handle_t), MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL);
    if (!wrapper_handle) {
        return 0;
    }
    wrapper_handle->id = type;
    wrapper_handle->handle = handle;
#ifdef ESP_MAP_ENABLE_CRC
    wrapper_handle->crc = crc16_le(0, (uint8_t const *)wrapper_handle, WRAP_HANDLE_SIZE);
#endif
    if (xSemaphoreTake(map_lock, TICKS_TO_WAIT) != pdPASS) {
        return 0;
    }
    int free_index = _esp_map_get_free_index();
    if (free_index == -1) {
        allocated_handles = heap_caps_realloc(allocated_handles, (2 * allocated_handle_size * SIZE_OF_HANDLE), MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL);
        if (!allocated_handles) {
            ESP_LOGE(TAG, "Failed to realloc handle array");
            abort();
        }
        /*
         * Realloc does not set allocated memory to 0. Initially allocated
         * memory is set to zero. Hence, call memset from allocated_handle_size
         * to end of allocated array.
         */
        memset(allocated_handles + allocated_handle_size, 0, (allocated_handle_size * SIZE_OF_HANDLE));
        allocated_handle_size *= 2;
        free_index = _esp_map_get_free_index();
    }
    allocated_handles[free_index] = (intptr_t) wrapper_handle;
    xSemaphoreGive(map_lock);
    return (ESP_MAP_INDEX_OFFSET + free_index);
}

/* Verify type identifier and CRC of the handle wrapper */
esp_map_handle_t *esp_map_verify(int wrapper_index, int type)
{
    /* esp_map APIs should not be called from ISR context. Hence, this assert is added */
    assert(xPortCanYield());
    if (wrapper_index < ESP_MAP_INDEX_OFFSET || wrapper_index > (ESP_MAP_INDEX_OFFSET + allocated_handle_size)) {
        return NULL;
    }
    if (xSemaphoreTake(map_lock, TICKS_TO_WAIT) != pdPASS) {
        return NULL;
    }
    esp_map_handle_t *wrapper_handle = ESP_MAP_GET_SHIM_HANDLE(wrapper_index);
    if (!is_valid_kdram_addr(wrapper_handle)) {
        goto error;
    }
    if (wrapper_handle->id != type) {
        goto error;
    }
#ifdef ESP_MAP_ENABLE_CRC
    uint16_t crc = crc16_le(0, (uint8_t const *)wrapper_handle, WRAP_HANDLE_SIZE);
    if (wrapper_handle->crc != crc) {
        goto error;
    }
#endif
    xSemaphoreGive(map_lock);
    return wrapper_handle;
error:
    xSemaphoreGive(map_lock);
    return NULL;
}

esp_map_handle_t *esp_map_get_handle(int wrapper_index)
{
    /* esp_map APIs should not be called from ISR context. Hence, this assert is added */
    assert(xPortCanYield());
    if (wrapper_index < ESP_MAP_INDEX_OFFSET || wrapper_index > (ESP_MAP_INDEX_OFFSET + allocated_handle_size)) {
        return NULL;
    }
    if (xSemaphoreTake(map_lock, TICKS_TO_WAIT) != pdPASS) {
        return NULL;
    }
    esp_map_handle_t *wrapper_handle = ESP_MAP_GET_SHIM_HANDLE(wrapper_index);
    if (!is_valid_kdram_addr(wrapper_handle)) {
        goto error;
    }
    xSemaphoreGive(map_lock);
    return wrapper_handle;
error:
    xSemaphoreGive(map_lock);
    return NULL;
}

void esp_map_remove(int wrapper_index)
{
    /* esp_map APIs should not be called from ISR context. Hence, this assert is added */
    assert(xPortCanYield());
    if (xSemaphoreTake(map_lock, TICKS_TO_WAIT) != pdPASS) {
        return;
    }
    esp_map_handle_t *wrapper_handle = ESP_MAP_GET_SHIM_HANDLE(wrapper_index);
    if (wrapper_handle) {
        memset(wrapper_handle, 0, sizeof(esp_map_handle_t));
        free(wrapper_handle);
        allocated_handles[wrapper_index - ESP_MAP_INDEX_OFFSET] = 0;
    }
    xSemaphoreGive(map_lock);
}

int esp_map_get_allocated_size(void)
{
    return allocated_handle_size;
}
