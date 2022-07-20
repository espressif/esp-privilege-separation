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

#include <freertos/FreeRTOS.h>

#include <string.h>
#include <app_ota.h>
#include <esp_console.h>
#include <syscall_wrappers.h>

static int _ota_url_handler(int argc, char *argv[])
{
    if (argc != 2) {
        return -1;
    }
    usr_esp_ota_user_app(argv[1], strlen(argv[1]));
    return 0;
}

esp_err_t app_ota_init(void)
{
    const esp_console_cmd_t debug_commands = {
        .command = "user-ota",
        .help = "User OTA URL",
        .func = _ota_url_handler,
    };
    return esp_console_cmd_register(&debug_commands);
}

