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

#include <lwip/sockets.h>
#include <lwip/netdb.h>

#include "user_config.h"
#include "user_example_connect.h"

#define WEB_SERVER "example.com"
#define WEB_PORT 80
#define WEB_URL "http://example.com/"

static const char *TAG = "user_main";

static const char *REQUEST = "GET " WEB_URL " HTTP/1.0\r\n"
    "Host: "WEB_SERVER"\r\n"
    "User-Agent: esp-idf/1.0 esp32\r\n"
    "\r\n";

const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
};
static struct addrinfo *res;
static int s, r;
static char recv_buf[64];

void http_request(void *args)
{
    while(1) {
        getaddrinfo(WEB_SERVER, "80", &hints, &res);

        s = socket(res->ai_family, res->ai_socktype, 0);
        if (s < 0) {
            ESP_LOGE(TAG, "Failed to allocate socket");
            freeaddrinfo(res);
            vTaskDelay(100);
            continue;
        }

        if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
            ESP_LOGE(TAG, "Socket connect failed");
            close(s);
            freeaddrinfo(res);
            vTaskDelay(100);
            continue;
        }

        freeaddrinfo(res);

        if (write(s, REQUEST, strlen(REQUEST)) < 0) {
            ESP_LOGE(TAG, "Write failed");
            close(s);
            vTaskDelay(100);
            continue;
        }

        do {
            memset(recv_buf, 0, sizeof(recv_buf));
            r = read(s, recv_buf, sizeof(recv_buf)-1);
            for(int i = 0; i < r; i++) {
                putchar(recv_buf[i]);
            }
        } while (r > 0);

        close(s);

        for (int i = 5; i > 0; i--) {
            ESP_LOGI(TAG, "Restarting in %d seconds", i);
            vTaskDelay(100);
        }
    }
}

void user_main()
{
    wifi_config_t wifi_conf = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS
        }
    };

    if(usr_wifi_connect(&wifi_conf) == ESP_OK) {
        if (xTaskCreate(http_request, "HTTP request", 8192, NULL, 1, NULL) != pdPASS) {
            ESP_LOGE(TAG, "Task Creation failed");
        }
    } else {
        ESP_LOGE(TAG, "WiFi initialization failed");
    }
}
