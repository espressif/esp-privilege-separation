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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"
#include "esp_priv_access_ota_utils.h"
#include "errno.h"
#include "esp_priv_access.h"
#include "esp_console.h"
#include "esp_secure_boot.h"

#ifdef CONFIG_IDF_TARGET_ESP32C3
#include "esp32c3/rom/crc.h"
#endif

#define BUFFSIZE 1024
#define MAX_URL_SIZE 150
#define USER_OTA_STACK_SIZE 5120

static const char *TAG = "esp_user_ota";
/*an ota data write buffer ready to write to the flash*/
static char ota_write_data[BUFFSIZE + 1] = { 0 };

static SemaphoreHandle_t _user_ota_sem;
static TaskHandle_t _user_ota_task_handle;
static QueueHandle_t _user_ota_queue;

static __attribute__((constructor)) void _ota_sem_init(void)
{
    _user_ota_sem = xSemaphoreCreateBinary();
    if (_user_ota_sem == NULL) {
        ESP_EARLY_LOGE(TAG, "Failed to create binary semaphore");
    } else {
        xSemaphoreGive(_user_ota_sem);
    }
}

static void http_cleanup(esp_http_client_handle_t client)
{
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}

static void user_ota_task(void *pvParameter)
{
    esp_err_t err;

    while (1) {
        /* update handle : set by esp_ota_begin(), must be freed via esp_ota_end() */
        esp_ota_handle_t update_handle = 0 ;
        const esp_partition_t *update_partition = NULL, *running = NULL;
        char url[MAX_URL_SIZE] = {};

        if (xQueueReceive(_user_ota_queue, &url, portMAX_DELAY) != pdPASS) {
            continue;
        }

        ESP_LOGI(TAG, "Starting User App OTA");

        esp_http_client_config_t config = {
            .url = url,
            .keep_alive_enable = true,
            .crt_bundle_attach = esp_crt_bundle_attach,
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client == NULL) {
            ESP_LOGE(TAG, "Failed to initialise HTTP connection");
            xSemaphoreGive(_user_ota_sem);
            continue;
        }
        err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
            esp_http_client_cleanup(client);
            xSemaphoreGive(_user_ota_sem);
            continue;
        }
        esp_http_client_fetch_headers(client);

        running = esp_priv_access_get_boot_partition();
        assert(running != NULL);

        update_partition = esp_priv_access_get_next_update_partition();
        assert(update_partition != NULL);

        int binary_file_length = 0;
        /*deal with all receive packet*/
        bool image_header_was_checked = false;
        while (1) {
            int data_read = esp_http_client_read(client, ota_write_data, BUFFSIZE);
            if (data_read < 0) {
                ESP_LOGE(TAG, "Error: SSL data read error");
                http_cleanup(client);
                xSemaphoreGive(_user_ota_sem);
                break;
            } else if (data_read > 0) {
                if (image_header_was_checked == false) {
                    esp_app_desc_t new_app_info;
                    if (data_read > sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
                        // check current version with downloading
                        memcpy(&new_app_info, &ota_write_data[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)], sizeof(esp_app_desc_t));
                        ESP_LOGI(TAG, "New firmware version: %s", new_app_info.version);

                        esp_app_desc_t running_app_info;
                        if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
                            ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
                        }

#ifndef CONFIG_ESP_USER_OTA_SKIP_VERSION_CHECK
                        if (memcmp(new_app_info.version, running_app_info.version, sizeof(new_app_info.version)) == 0) {
                            ESP_LOGW(TAG, "Current running version is the same as a new. We will not continue the update.");
                            http_cleanup(client);
                            xSemaphoreGive(_user_ota_sem);
                            break;
                        }
#endif

                        esp_image_header_t img_hdr;
                        memcpy(&img_hdr, ota_write_data, sizeof(esp_image_header_t));
                        if (img_hdr.chip_id != CONFIG_IDF_FIRMWARE_CHIP_ID) {
                            ESP_LOGE(TAG, "Mismatch chip id, expected %d, found %d", CONFIG_IDF_FIRMWARE_CHIP_ID, img_hdr.chip_id);
                            http_cleanup(client);
                            esp_ota_abort(update_handle);
                            xSemaphoreGive(_user_ota_sem);
                            break;
                        }

                        image_header_was_checked = true;

                        err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
                        if (err != ESP_OK) {
                            ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
                            http_cleanup(client);
                            esp_ota_abort(update_handle);
                            xSemaphoreGive(_user_ota_sem);
                            break;
                        }
                        ESP_LOGI(TAG, "esp_ota_begin succeeded");
                    } else {
                        ESP_LOGE(TAG, "received package is not fit len");
                        http_cleanup(client);
                        esp_ota_abort(update_handle);
                        xSemaphoreGive(_user_ota_sem);
                        break;
                    }
                }
                err = esp_ota_write(update_handle, (const void *)ota_write_data, data_read);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to write data to OTA partition, 0x%x", err);
                    http_cleanup(client);
                    esp_ota_abort(update_handle);
                    xSemaphoreGive(_user_ota_sem);
                    break;
                }
                binary_file_length += data_read;
                ESP_LOGD(TAG, "Written image length %d", binary_file_length);
            } else if (data_read == 0) {
               /*
                * As esp_http_client_read never returns negative error code, we rely on
                * `errno` to check for underlying transport connectivity closure if any
                */
                if (errno == ECONNRESET || errno == ENOTCONN) {
                    ESP_LOGE(TAG, "Connection closed, errno = %d", errno);
                    break;
                }
                if (esp_http_client_is_complete_data_received(client) == true) {
                    ESP_LOGI(TAG, "Connection closed");
                    break;
                }
            }
        }
        ESP_LOGI(TAG, "Total Write binary data length: %d", binary_file_length);
        if (esp_http_client_is_complete_data_received(client) != true) {
            ESP_LOGE(TAG, "Error in receiving complete file");
            http_cleanup(client);
            esp_ota_abort(update_handle);
            xSemaphoreGive(_user_ota_sem);
            continue;
        }

        err = esp_ota_end(update_handle);
        if (err != ESP_OK) {
            if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
                ESP_LOGE(TAG, "Image validation failed, image is corrupted");
            }
            ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
            http_cleanup(client);
            xSemaphoreGive(_user_ota_sem);
            continue;
        }

#if !defined(CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT) && defined(CONFIG_PA_ENABLE_USER_APP_SECURE_BOOT)
        /* When protected secure boot or signed OTA is enabled, the signature check will be done by esp_ota_end.
         * Hence, verifying the signature here again becomes redundant.
         * Explicitly verify the digital signature of user app if in case protected app secure boot or signed OTA is disabled.
         */
        err = esp_priv_access_verify_user_app(update_partition);
        if (err != ESP_OK) {
            if (err == ESP_ERR_IMAGE_INVALID) {
                ESP_LOGE(TAG, "Image verification failed");
            }
            ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
            http_cleanup(client);
            xSemaphoreGive(_user_ota_sem);
            continue;
        }
#endif

        err = esp_priv_access_set_boot_partition(update_partition);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
            http_cleanup(client);
            xSemaphoreGive(_user_ota_sem);
            continue;
        }

        xSemaphoreGive(_user_ota_sem);

        ESP_LOGI(TAG, "Prepare to restart system!");
        esp_restart();
    }
}

esp_err_t esp_user_ota_start(char *url)
{
    if (_user_ota_sem == NULL || url == NULL) {
        return ESP_FAIL;
    }

    if (xSemaphoreTake(_user_ota_sem, 0) != pdTRUE) {
        ESP_LOGE(TAG, "User app OTA already in progress");
        return ESP_FAIL;
    }

    if (_user_ota_queue == NULL) {
        _user_ota_queue = xQueueCreate(1, sizeof(char) * 150);
        if (_user_ota_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create OTA queue");
            return ESP_ERR_NO_MEM;
        }
    }

    if (_user_ota_task_handle == NULL) {
        xTaskCreate(&user_ota_task, "user_ota_task", USER_OTA_STACK_SIZE, NULL, 5, &_user_ota_task_handle);
        if (_user_ota_task_handle == NULL) {
            ESP_LOGE(TAG, "Failed to create user_ota_task");
            xSemaphoreGive(_user_ota_sem);
            return ESP_ERR_NO_MEM;
        }
    }

    char ota_url[MAX_URL_SIZE] = {};
    snprintf(ota_url, sizeof(ota_url), "%s", url);
    if (xQueueSend(_user_ota_queue, ota_url, 0) != pdPASS) {
        xSemaphoreGive(_user_ota_sem);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* When protected app secure OTA or secure boot is enabled and user app secure boot is disabled,
 * esp_ota_end will internally try to verify the digital signature for user app as well, which should not happen.
 * To avoid that, we wrap esp_secure_boot_verify_rsa_signature_block and skip the signature check for user app.
 *
 * When user app secure boot is enabled, this definition of __wrap_esp_secure_boot_verify_rsa_signature_block is
 * not picked up, instead the one defined in esp_priv_access_secure_boot.c is picked up.
 */
#if defined(CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT) && !defined(CONFIG_PA_ENABLE_USER_APP_SECURE_BOOT)

extern esp_err_t __real_esp_secure_boot_verify_rsa_signature_block(const ets_secure_boot_signature_t *sig_block, const uint8_t *image_digest, uint8_t *verified_digest);

esp_err_t __wrap_esp_secure_boot_verify_rsa_signature_block(const ets_secure_boot_signature_t *sig_block, const uint8_t *image_digest, uint8_t *verified_digest)
{
    if (_user_ota_sem != NULL) {
        if (xSemaphoreTake(_user_ota_sem, 0) != pdTRUE) {
            /* User app OTA is in progress and since user app secure boot is not enabled,
             * we return ESP_OK;
             */
            return ESP_OK;
        } else {
            xSemaphoreGive(_user_ota_sem);
        }
    }

    /* In all other cases, its the protected app update that's happening.
     * Call the real verify function to check the digital signature of the protected app.
     */
    return __real_esp_secure_boot_verify_rsa_signature_block(sig_block, image_digest, verified_digest);
}
#endif
