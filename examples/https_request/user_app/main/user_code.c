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

#include <lwip/sockets.h>
#include <lwip/netdb.h>

#include "esp_tls.h"

#include "nvs_flash.h"
#include "freertos/event_groups.h"
#include "user_config.h"

/* Constants that aren't configurable in menuconfig */
#define WEB_SERVER "www.howsmyssl.com"
#define WEB_PORT "443"
#define WEB_URL "https://www.howsmyssl.com/a/check"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

extern const uint8_t server_root_cert_pem_start[] asm("_binary_server_root_cert_pem_start");
extern const uint8_t server_root_cert_pem_end[]   asm("_binary_server_root_cert_pem_end");

static const char REQUEST[] = "GET " WEB_URL " HTTP/1.1\r\n"
                             "Host: "WEB_SERVER"\r\n"
                             "User-Agent: esp-idf/1.0 esp32\r\n"
                             "\r\n";

static EventGroupHandle_t wifi_event_group;

static int retries = 0;

static wifi_config_t wifi_conf = {
    .sta = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASS
    }
};

UIRAM_ATTR void event_handler(void* arg, usr_esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT_BASE && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT_BASE && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (retries < 5) {
            esp_wifi_connect();
            printf("Retry to connect to the AP\n");
            retries++;
        } else {
            printf("Failed to connect to the AP\n");
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
        printf("Error nvs_flash_init: %d\n", ret);
        goto exit;
    }

    ret |= esp_netif_init();
    if (ret != 0) {
        printf("usr_esp_netif_init failed: %d\n", ret);
        goto exit;
    }
    ret |= esp_event_loop_create_default();
    if (ret != 0) {
        printf("usr_event_loop_create_default failed: %d\n", ret);
        goto exit;
    }

    esp_netif_create_default_wifi_sta();

    ret |= esp_wifi_init(&cfg);
    if (ret != 0) {
        printf("usr_esp_wifi_init failed: %d\n", ret);
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
        printf("Ret: %d\n", ret);
        goto cleanup;
    }

    ret |= esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_conf);
    if (ret != 0) {
        printf("Ret: %d\n", ret);
        goto cleanup;
    }

    ret |= esp_wifi_start();
    if (ret != 0) {
        printf("Ret: %d\n", ret);
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
    usr_vEventGroupDelete(wifi_event_group);
    return ret;
}

static void https_request(esp_tls_cfg_t cfg)
{
    char buf[512];
    int ret, len = 0;

    struct esp_tls *tls = esp_tls_conn_http_new(WEB_URL, &cfg);

    if (tls != NULL) {
        printf("Connection established...\n");
    } else {
        printf("Connection failed...\n");
        goto exit;
    }

    size_t written_bytes = 0;
    do {
        ret = usr_esp_tls_conn_write(tls,
                                 REQUEST + written_bytes,
                                 sizeof(REQUEST) - written_bytes);
        if (ret >= 0) {
            printf("%d bytes written\n", ret);
            written_bytes += ret;
        } else if (ret != ESP_TLS_ERR_SSL_WANT_READ  && ret != ESP_TLS_ERR_SSL_WANT_WRITE) {
            printf("esp_tls_conn_write  returned: [0x%02X]\n", ret);
            goto exit;
        }
    } while (written_bytes < sizeof(REQUEST));

    printf("Reading HTTP response...\n");

    do {
        len = sizeof(buf) - 1;
        usr_memset(buf, 0x0, sizeof(buf));
        ret = usr_esp_tls_conn_read(tls, (char *)buf, len);

        if (ret == ESP_TLS_ERR_SSL_WANT_WRITE  || ret == ESP_TLS_ERR_SSL_WANT_READ) {
            continue;
        }

        if (ret < 0) {
            printf("esp_tls_conn_read  returned [-0x%02X]\n", -ret);
            break;
        }

        if (ret == 0) {
            printf("Connection closed\n");
            break;
        }

        len = ret;
        printf("%d bytes read\n", len);
        /* Print response directly to stdout as it is read */
        for (int i = 0; i < len; i++) {
            putchar(buf[i]);
        }
        putchar('\n'); // JSON output doesn't have a newline at end
    } while (1);

exit:
    esp_tls_conn_delete(tls);
}

void user_main()
{
    wifi_event_group = xEventGroupCreate();
    if(usr_wifi_init() == ESP_OK) {
        esp_tls_cfg_t cfg = {
            .cacert_buf = (const unsigned char *) server_root_cert_pem_start,
            .cacert_bytes = server_root_cert_pem_end - server_root_cert_pem_start,
        };

        https_request(cfg);
    } else {
        printf("WiFi initialization failed\n");
    }
}
