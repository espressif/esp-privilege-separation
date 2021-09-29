// Copyright 2020 Espressif Systems (Shanghai) PTE LTD
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

/* Privilege Separation Example:
 *
 * This file is for protected application.
 * Developer can write routines which need to execute in protected space
 * In order to start user application, initialize esp_ps and call esp_ps_user_boot
 */

#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ps.h"

#define TAG             "protected_app"

PS_INTR_ATTR void world_monitor_isr(void *arg)
{
    esp_ps_int_t intr = esp_ps_get_int_status();
    ets_printf("Illegal %s access: Fault addr: 0x%x\n", esp_ps_int_type_to_str(intr), esp_ps_get_fault_addr(intr));
    esp_ps_clear_and_reenable_int(intr);
}

IRAM_ATTR void app_main()
{
    esp_err_t ret;

    ret = esp_ps_init(world_monitor_isr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize PS %d\n", ret);
    }

    esp_ps_set_periph_perm(PS_GPIO, PS_WORLD_1, PS_PERM_ALL);

    ret = esp_ps_user_boot();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to boot user app %d\n", ret);
    }

    for (int i = 0; ; i++) {
       ets_printf("Hello from protected environment\n");
       vTaskDelay(500);
    }
}
