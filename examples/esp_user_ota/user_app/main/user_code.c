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

#include "esp_log.h"
#include "esp_console.h"

#include "user_config.h"
#include "user_example_connect.h"

static const char *TAG = "user_main";

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

    wifi_config_t wifi_conf = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS
        }
    };

    if (usr_wifi_connect(&wifi_conf) != ESP_OK) {
        ESP_LOGE(TAG, "WiFi initialization failed");
    }
}
