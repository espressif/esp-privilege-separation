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

#include <string.h>
#include <esp_image_format.h>
#include <esp_spi_flash.h>
#include <esp_partition.h>
#include "esp_priv_access_ota_utils.h"
#include "esp_log.h"
#include "esp_fault.h"

#if CONFIG_IDF_TARGET_ESP32C3
#include "esp32c3/rom/cache.h"
#include "esp32c3/rom/efuse.h"
#include "esp32c3/rom/ets_sys.h"
#include "esp32c3/rom/spi_flash.h"
#include "esp32c3/rom/crc.h"
#include "esp32c3/rom/rtc.h"
#include "esp32c3/rom/uart.h"
#include "esp32c3/rom/gpio.h"
#include "esp32c3/rom/secure_boot.h"
#elif CONFIG_IDF_TARGET_ESP32S3
#include "esp32s3/rom/cache.h"
#include "esp32s3/rom/efuse.h"
#include "esp32s3/rom/ets_sys.h"
#include "esp32s3/rom/spi_flash.h"
#include "esp32s3/rom/crc.h"
#include "esp32s3/rom/rtc.h"
#include "esp32s3/rom/uart.h"
#include "esp32s3/rom/gpio.h"
#include "esp32s3/rom/secure_boot.h"
#endif
#include "soc/world_controller_reg.h"
#include "soc/sensitive_reg.h"
#include "soc/extmem_reg.h"
#include "soc/cache_memory.h"
#include "soc/soc.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc_defs.h"

#include "esp_priv_access_priv.h"
#include "esp_priv_access.h"

#define TAG "esp_priv_access_image_utility"

/* Cache MMU block size */
#define MMU_BLOCK_SIZE    0x00010000

/* Cache MMU address mask (MMU tables ignore bits which are zero) */
#define MMU_FLASH_MASK    (~(MMU_BLOCK_SIZE - 1))

#define MAP_ERR_MSG "Image contains multiple %s segments. Only the last one will be mapped."

static inline uint32_t user_cache_pages_to_map(uint32_t size, uint32_t vaddr)
{
    return (size + (vaddr - (vaddr & MMU_FLASH_MASK)) + MMU_BLOCK_SIZE - 1) / MMU_BLOCK_SIZE;
}

static IRAM_ATTR uint32_t kernel_cache_disable()
{
#if CONFIG_IDF_TARGET_ESP32C3
    uint32_t icache_state = Cache_Suspend_ICache() << 16;
    return icache_state;
#elif CONFIG_IDF_TARGET_ESP32S3
    uint32_t dcache_state = Cache_Suspend_DCache();
    Cache_Invalidate_DCache_All();
    return dcache_state;
#endif
}

static IRAM_ATTR void kernel_cache_restore(uint32_t cache_state)
{
#if CONFIG_IDF_TARGET_ESP32C3
    Cache_Resume_ICache(cache_state >> 16);
#elif CONFIG_IDF_TARGET_ESP32S3
    REG_CLR_BIT(EXTMEM_DCACHE_CTRL1_REG, EXTMEM_DCACHE_SHUT_CORE0_BUS);
#if !CONFIG_FREERTOS_UNICORE
    REG_CLR_BIT(EXTMEM_DCACHE_CTRL1_REG, EXTMEM_DCACHE_SHUT_CORE1_BUS);
#endif
    Cache_Resume_DCache(cache_state);
#endif
}

/*
 * user_load_app function is converted into inline with -Os
 * optimization flag. Inline function discards IRAM_ATTR attribute,
 * resulting in a crash. NOINLINE_ATTR ensures that the function is
 * not converted into inline.
 */
static IRAM_ATTR NOINLINE_ATTR void user_load_app(const esp_image_metadata_t* data)
{
    uint32_t drom_addr = 0;
    uint32_t drom_load_addr = 0;
    uint32_t drom_size = 0;
    uint32_t irom_addr = 0;
    uint32_t irom_load_addr = 0;
    uint32_t irom_size = 0;

    // Find DROM & IROM addresses, to configure cache mappings
    for (int i = 0; i < data->image.segment_count; i++) {
        const esp_image_segment_header_t *header = &data->segments[i];
        if (header->load_addr >= SOC_UDROM_LOW && header->load_addr < SOC_UDROM_HIGH) {
            if (drom_addr != 0) {
                ESP_LOGE(TAG, MAP_ERR_MSG, "DROM");
            } else {
                ESP_LOGI(TAG, "Mapping segment %d as %s", i, "DROM");
            }
            drom_addr = data->start_addr + data->segment_data[i];
            drom_load_addr = header->load_addr;
            drom_size = header->data_len;
        }
        if (header->load_addr >= SOC_UIROM_LOW && header->load_addr < SOC_UIROM_HIGH) {
            if (irom_addr != 0) {
                ESP_LOGE(TAG, MAP_ERR_MSG, "IROM");
            } else {
                ESP_LOGI(TAG, "Mapping segment %d as %s", i, "IROM");
            }
            irom_addr = data->start_addr + data->segment_data[i];
            irom_load_addr = header->load_addr;
            irom_size = header->data_len;
        }
    }

    uint32_t ret = 0 ;

    ret = kernel_cache_disable();

    uint32_t drom_page_count = user_cache_pages_to_map(drom_size, drom_load_addr);

    Cache_Dbus_MMU_Set(MMU_ACCESS_FLASH, drom_load_addr & MMU_FLASH_MASK, drom_addr & MMU_FLASH_MASK, 64, drom_page_count, 0);

    uint32_t irom_page_count = user_cache_pages_to_map(irom_size, irom_load_addr);

    Cache_Ibus_MMU_Set(MMU_ACCESS_FLASH, irom_load_addr & MMU_FLASH_MASK, irom_addr & MMU_FLASH_MASK, 64, irom_page_count, 0);

    kernel_cache_restore(ret);
}

esp_err_t esp_priv_access_user_unpack(esp_image_metadata_t *user_img_data)
{
    const esp_partition_t *user_partition = esp_priv_access_get_boot_partition();
    if (user_partition == NULL) {
        ESP_LOGW(TAG, "User code partition not found");
        return ESP_ERR_NOT_FOUND;
    } else {
#if CONFIG_PA_ENABLE_USER_APP_SECURE_BOOT
        esp_err_t err = ESP_ERR_IMAGE_INVALID;
        err = esp_priv_access_verify_user_app(user_partition);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "User app signature invalid");
            return ESP_ERR_IMAGE_INVALID;
        } else {
            ESP_FAULT_ASSERT(err == ESP_OK);
        }
#endif

        ESP_LOGD(TAG, "User code partition @ 0x%x (0x%x)", user_partition->address, user_partition->size);

        user_img_data->start_addr = user_partition->address;
        esp_partition_read(user_partition, 0x0, &user_img_data->image, sizeof(esp_image_header_t));

        if (is_valid_user_i_addr((void *)user_img_data->image.entry_addr)) {
            uint32_t next_addr = sizeof(esp_image_header_t);
            for(int i = 0; i < user_img_data->image.segment_count; i++) {
                esp_partition_read(user_partition, next_addr, &user_img_data->segments[i], sizeof(esp_image_segment_header_t));
                next_addr += sizeof(esp_image_segment_header_t);
                user_img_data->segment_data[i] = next_addr;
                next_addr += user_img_data->segments[i].data_len;
            }

            user_img_data->image_len = next_addr - user_img_data->start_addr;

            for (int i = 0; i < user_img_data->image.segment_count; i++) {
                uint32_t *load_addr = (uint32_t *)user_img_data->segments[i].load_addr;
                if (is_valid_uiram_addr(load_addr) || is_valid_udram_addr((void *)load_addr)) {
                    ESP_LOGI(TAG, "Section loading at vaddr:%p paddr:0x%x (%d)", load_addr,
                            user_img_data->start_addr + user_img_data->segment_data[i], user_img_data->segments[i].data_len);

                    spi_flash_read(user_partition->address + user_img_data->segment_data[i],
                            load_addr, user_img_data->segments[i].data_len);
                }
            }
            user_load_app(user_img_data);
            return ESP_OK;
        }

        return ESP_FAIL;
    }
}

esp_err_t esp_priv_access_load_user_app_desc(usr_custom_app_desc_t *app_desc)
{
    const esp_partition_t *user_partition = esp_priv_access_get_boot_partition();

    if (user_partition == NULL) {
        ESP_LOGE(TAG, "User code partition not found");
        return ESP_ERR_NOT_FOUND;
    }

    uint32_t offset = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);

    esp_err_t ret = esp_partition_read(user_partition, offset, app_desc, sizeof(usr_custom_app_desc_t));

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error reading user partition");
    }

    return ret;
}
