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
#include <freertos/FreeRTOS.h>
#include <esp_priv_access_ota_utils.h>
#include <esp_image_format.h>
#include <esp_ota_ops.h>
#include <esp_log.h>

#ifdef CONFIG_IDF_TARGET_ESP32C3
#include <esp32c3/rom/crc.h>
#elif CONFIG_IDF_TARGET_ESP32S3
#include <esp32s3/rom/crc.h>
#endif

static const char *TAG = "ota_utils";

#ifdef CONFIG_PA_ENABLE_USER_APP_ROLLBACK
static uint8_t new_app_slot;
#endif

static const esp_partition_t *_read_user_otadata(esp_pa_ota_select_entry_t *two_otadata)
{
    const esp_partition_t *user_otadata = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_USER_OTA, NULL);
    if (!user_otadata) {
        ESP_LOGW(TAG, "User otadata partition not Found");
        return NULL;
    }
    spi_flash_mmap_handle_t ota_data_map;
    const void *result = NULL;

    esp_err_t err = esp_partition_mmap(user_otadata, 0, user_otadata->size, SPI_FLASH_MMAP_DATA, &result, &ota_data_map);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "User otadata mmap filed. Err=0x%8x", err);
        return NULL;
    }
    memcpy(&two_otadata[0], result, sizeof(esp_pa_ota_select_entry_t));
    memcpy(&two_otadata[1], result + SPI_FLASH_SEC_SIZE, sizeof(esp_pa_ota_select_entry_t));
    spi_flash_munmap(ota_data_map);
    return user_otadata;
}

static esp_err_t _esp_priv_access_update_otadata_sector(esp_pa_ota_select_entry_t *otadata, size_t offset)
{
    const esp_partition_t *otadata_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_USER_OTA, NULL);

    esp_err_t err = esp_partition_erase_range(otadata_partition, offset, SPI_FLASH_SEC_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase otadata, 0x%x", err);
        return err;
    }
    err = esp_partition_write(otadata_partition, offset, otadata, sizeof(esp_pa_ota_select_entry_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write otadata, 0x%x", err);
        return err;
    }
    return ESP_OK;
}

static esp_err_t _esp_priv_access_update_otadata(esp_pa_ota_select_entry_t *otadata)
{
    esp_err_t err = _esp_priv_access_update_otadata_sector(otadata, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update first otadata sector, 0x%x", err);
        return err;
    }
    err = _esp_priv_access_update_otadata_sector(otadata, SPI_FLASH_SEC_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update first otadata sector, 0x%x", err);
        return err;
    }
    return ESP_OK;
}

static esp_err_t _get_valid_user_otadata(esp_pa_ota_select_entry_t *otadata)
{
    esp_pa_ota_select_entry_t two_otadata[2];
    if (_read_user_otadata(two_otadata) == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_pa_ota_select_entry_t blank_otadata;
    memset(&blank_otadata, 0xff, sizeof(esp_pa_ota_select_entry_t));

    // Check if the contents of both the otadata sectors match
    if (!memcmp(&two_otadata[0], &two_otadata[1], sizeof(esp_pa_ota_select_entry_t))) {
        if (!memcmp(&two_otadata[0], &blank_otadata, sizeof(esp_pa_ota_select_entry_t))) {
            memcpy(otadata, &two_otadata[0], sizeof(esp_pa_ota_select_entry_t));
            return ESP_OK;
        }

        uint32_t crc = crc32_le(0, (uint8_t const *)two_otadata, (sizeof(esp_pa_ota_select_entry_t) - sizeof(uint32_t)));
        if (two_otadata[0].magic != USER_OTADATA_MAGIC || crc != two_otadata[0].crc) {
            ESP_LOGE(TAG, "User otadata[0] magic and CRC verification failed");
            return ESP_FAIL;
        }
        memcpy(otadata, &two_otadata[0], sizeof(esp_pa_ota_select_entry_t));
    } else {
        uint32_t crc_otadata0 = crc32_le(0, (uint8_t const *)&two_otadata[0], (sizeof(esp_pa_ota_select_entry_t) - sizeof(uint32_t)));
        uint32_t crc_otadata1 = crc32_le(0, (uint8_t const *)&two_otadata[1], (sizeof(esp_pa_ota_select_entry_t) - sizeof(uint32_t)));
        if (crc_otadata0 == two_otadata[0].crc) {
            ESP_LOGW(TAG, "Second otadata sector is invalid!");
            // Copy contents of first otadata sector into second
            _esp_priv_access_update_otadata_sector(&two_otadata[0], SPI_FLASH_SEC_SIZE);
            memcpy(otadata, &two_otadata[0], sizeof(esp_pa_ota_select_entry_t)); 
        } else if (crc_otadata1 == two_otadata[1].crc) {
            ESP_LOGW(TAG, "First otadata sector is invalid!");
            // Copy contents of second otadata sector into first
            _esp_priv_access_update_otadata_sector(&two_otadata[1], 0);
            memcpy(otadata, &two_otadata[1], sizeof(esp_pa_ota_select_entry_t));
        } else {
            ESP_LOGE(TAG, "Both otadata sectors are invalid, aborting...");
            abort();
        }
    }
    return ESP_OK;
}

const esp_partition_t *esp_priv_access_get_boot_partition(void)
{
    esp_pa_ota_select_entry_t otadata = {}, blank_otadata;
    esp_err_t err = _get_valid_user_otadata(&otadata);
    if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGD(TAG, "otadata partition not found, booting from first partition");
        return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_USER_0, NULL);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get vaild otadata, 0x%x", err);
        return NULL;
    }
    memset(&blank_otadata, 0xff, sizeof(esp_pa_ota_select_entry_t));
    if (!memcmp(&blank_otadata, &otadata, sizeof(esp_pa_ota_select_entry_t))) {
        return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_USER_0, NULL);
    }

    uint32_t boot_partition = 0;
#ifdef CONFIG_PA_ENABLE_USER_APP_ROLLBACK
    if (otadata.try_partition != UINT8_MAX) {
        boot_partition = otadata.try_partition;
        new_app_slot = otadata.try_partition;
        otadata.try_partition = -1;
        otadata.crc = crc32_le(0, (uint8_t const *)&otadata, (sizeof(esp_pa_ota_select_entry_t) - sizeof(uint32_t)));
        _esp_priv_access_update_otadata(&otadata);
    } else {
#endif
        boot_partition = otadata.boot_partition;
#ifdef CONFIG_PA_ENABLE_USER_APP_ROLLBACK
    }
#endif
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, boot_partition, NULL); 
}

const esp_partition_t *esp_priv_access_get_next_update_partition(void)
{
    esp_pa_ota_select_entry_t otadata = {}, blank_otadata;
    esp_err_t err = _get_valid_user_otadata(&otadata);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get vaild otadata, 0x%x", err);
        return NULL;
    }
    memset(&blank_otadata, 0xff, sizeof(esp_pa_ota_select_entry_t));
    if (!memcmp(&blank_otadata, &otadata, sizeof(esp_pa_ota_select_entry_t))) {
        return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_USER_1, NULL);
    }

    uint32_t update_partition = (otadata.boot_partition == ESP_PARTITION_SUBTYPE_APP_USER_0) ? ESP_PARTITION_SUBTYPE_APP_USER_1 : ESP_PARTITION_SUBTYPE_APP_USER_0;
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, update_partition, NULL);
}

static esp_err_t image_validate(const esp_partition_t *partition, esp_image_load_mode_t load_mode)
{
    esp_image_metadata_t data;
    const esp_partition_pos_t part_pos = {
        .offset = partition->address,
        .size = partition->size,
    };

    if (esp_image_verify(load_mode, &part_pos, &data) != ESP_OK) {
        return ESP_ERR_OTA_VALIDATE_FAILED;
    }
    return ESP_OK;
}

esp_err_t esp_priv_access_set_boot_partition(const esp_partition_t *partition)
{
    if (partition == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (image_validate(partition, ESP_IMAGE_VERIFY) != ESP_OK) {
        return ESP_ERR_OTA_VALIDATE_FAILED;
    }

    esp_pa_ota_select_entry_t otadata = {};
    otadata.magic = USER_OTADATA_MAGIC;
#ifdef CONFIG_PA_ENABLE_USER_APP_ROLLBACK
    const esp_partition_t *boot_partition = esp_priv_access_get_boot_partition(); 
    otadata.boot_partition = boot_partition->subtype;
    otadata.try_partition = partition->subtype;
#else
    otadata.boot_partition = partition->subtype;
#endif
    otadata.crc = crc32_le(0, (uint8_t const *)&otadata, (sizeof(esp_pa_ota_select_entry_t) - sizeof(uint32_t)));

    return _esp_priv_access_update_otadata(&otadata); 
}

#ifdef CONFIG_PA_ENABLE_USER_APP_ROLLBACK
esp_err_t esp_priv_access_mark_user_app_valid_and_cancel_rollback(void)
{
    esp_err_t err = ESP_OK;
    if (!new_app_slot) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_pa_ota_select_entry_t two_otadata[2] = {};
    if (_read_user_otadata(two_otadata) == NULL) {
        return ESP_FAIL;
    }
    two_otadata[0].boot_partition = new_app_slot;
    two_otadata[0].crc = crc32_le(0, (uint8_t const *)two_otadata, (sizeof(esp_pa_ota_select_entry_t) - sizeof(uint32_t)));
    if ((err = _esp_priv_access_update_otadata(&two_otadata[0])) != ESP_OK) {
        return err;
    }
    new_app_slot = 0;
    return ESP_OK;
}
#endif
