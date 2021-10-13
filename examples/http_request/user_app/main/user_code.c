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

#include <lwip/sockets.h>
#include <lwip/netdb.h>

#include "nvs_flash.h"
#include "freertos/event_groups.h"
#include "user_config.h"

#define WEB_SERVER "example.com"
#define WEB_PORT 80
#define WEB_URL "http://example.com/"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "user_main";

static EventGroupHandle_t wifi_event_group;

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

static int retries = 0;

static wifi_config_t wifi_conf = {
    .sta = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASS
    }
};

void http_request();

UIRAM_ATTR void event_handler(void* arg, usr_esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT_BASE && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT_BASE && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (retries < 5) {
            esp_wifi_connect();
            ESP_LOGE(TAG, "Retry to connect to the AP");
            retries++;
        } else {
            ESP_LOGE(TAG, "Failed to connect to the AP");
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT_BASE && event_id == IP_EVENT_STA_GOT_IP) {
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

    usr_esp_event_handler_instance_t instance_any_id;
    usr_esp_event_handler_instance_t instance_got_ip;

    usr_esp_event_handler_instance_register(WIFI_EVENT_BASE,
                                            ESP_EVENT_ANY_ID,
                                            &event_handler,
                                            NULL,
                                            &instance_any_id);

    usr_esp_event_handler_instance_register(IP_EVENT_BASE,
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
    usr_esp_event_handler_instance_unregister(IP_EVENT_BASE, IP_EVENT_STA_GOT_IP, instance_got_ip);
    usr_esp_event_handler_instance_unregister(WIFI_EVENT_BASE, ESP_EVENT_ANY_ID, instance_any_id);
exit:
    vEventGroupDelete(wifi_event_group);
    return ret;
}

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
    wifi_event_group = xEventGroupCreate();
    if(usr_wifi_init() == ESP_OK) {
        if (xTaskCreate(http_request, "HTTP request", 8192, NULL, 1, NULL) != pdPASS) {
            ESP_LOGE(TAG, "Task Creation failed");
        }
    } else {
        ESP_LOGE(TAG, "WiFi initialization failed");
    }
}
