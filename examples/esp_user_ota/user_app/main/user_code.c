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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "syscall_wrappers.h"

#include "esp_netif_types.h"
#include "esp_wifi_types.h"
#include "esp_event_base.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_console.h"

#include "nvs_flash.h"
#include "freertos/event_groups.h"
#include "user_config.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "user_main";

static EventGroupHandle_t wifi_event_group;

static int retries = 0;

static wifi_config_t wifi_conf = {
    .sta = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASS
    }
};

UIRAM_ATTR void event_handler(void* arg, esp_event_base_t event_base,
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

esp_err_t usr_wifi_init()
{
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
    ret |= esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_conf);
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

static int _ota_url_handler(int argc, char *argv[])
{
    if (argc != 2) {
        return -1;
    }
    usr_esp_ota_user_app(argv[1], strlen(argv[1]));
    return 0;
}

void user_main()
{
    const esp_console_cmd_t debug_commands = {
        .command = "user-ota",
        .help = "User OTA URL",
        .func = _ota_url_handler,
    };

    esp_err_t err = esp_console_cmd_register(&debug_commands);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register user-ota command, 0x%x", err);
    }

    wifi_event_group = xEventGroupCreate();
    if (usr_wifi_init() != ESP_OK) {
        ESP_LOGE(TAG, "WiFi initialization failed");
    }
}
