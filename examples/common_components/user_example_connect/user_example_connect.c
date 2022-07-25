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
#include <syscall_wrappers.h>

#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_netif_types.h>
#include <esp_wifi_types.h>
#include <esp_event_base.h>
#include <esp_wifi.h>
#include <esp_event.h>

#include <freertos/event_groups.h>

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t wifi_event_group;
static const char *TAG = "user_example_common";

static int retries = 0;

void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (retries < 5) {
            esp_wifi_connect();
            ESP_LOGE(TAG, "Retry to connect to the AP");
            retries++;
        } else {
            ESP_LOGE(TAG, "Failed to connect to the AP");
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t usr_wifi_connect(wifi_config_t *wifi_conf)
{
    wifi_event_group = xEventGroupCreate();
    wifi_init_config_t cfg;

    int ret = nvs_flash_init();
    if (ret != 0) {
        ESP_LOGE(TAG, "Error nvs_flash_init: %d", ret);
        goto exit;
    }

    ret |= esp_netif_init();
    if (ret != 0) {
        ESP_LOGE(TAG, "usr_esp_netif_init failed: %d", ret);
        goto exit;
    }
    ret |= esp_event_loop_create_default();
    if (ret != 0) {
        ESP_LOGE(TAG, "usr_event_loop_create_default failed: %d", ret);
        goto exit;
    }

    esp_netif_create_default_wifi_sta();

    ret |= esp_wifi_init(&cfg);
    if (ret != 0) {
        ESP_LOGE(TAG, "usr_esp_wifi_init failed: %d", ret);
        goto exit;
    }

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;

    esp_event_handler_instance_register(WIFI_EVENT,
                                        ESP_EVENT_ANY_ID,
                                        &event_handler,
                                        NULL,
                                        &instance_any_id);

    esp_event_handler_instance_register(IP_EVENT,
                                        IP_EVENT_STA_GOT_IP,
                                        &event_handler,
                                        NULL,
                                        &instance_got_ip);

    ret |= esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != 0) {
        ESP_LOGE(TAG, "Ret: %d", ret);
        goto cleanup;
    }
    ret |= esp_wifi_set_config(ESP_IF_WIFI_STA, wifi_conf);
    if (ret != 0) {
        ESP_LOGE(TAG, "Ret: %d", ret);
        goto cleanup;
    }
    ret |= esp_wifi_start();
    if (ret != 0) {
        ESP_LOGE(TAG, "Ret: %d", ret);
        goto cleanup;
    }

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                false, false, portMAX_DELAY);

    if (bits & WIFI_FAIL_BIT) {
        ret = -1;
    }

cleanup:
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip);
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id);
exit:
    vEventGroupDelete(wifi_event_group);
    return ret;
}
