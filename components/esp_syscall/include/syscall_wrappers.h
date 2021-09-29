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

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "hal/gpio_types.h"
#include "driver/gpio.h"

#include "esp_event.h"
#include "esp_wifi.h"

#include <lwip/sockets.h>
#include <lwip/netdb.h>

#include "esp_tls.h"

#include "soc_defs.h"

typedef void* usr_esp_event_handler_instance_t;
typedef void* usr_gpio_handle_t;

typedef enum {
    WIFI_EVENT_BASE = 0,
    IP_EVENT_BASE,
} usr_esp_event_base_t;

typedef void (*usr_esp_event_handler_t)(void* event_handler_arg,
                                        usr_esp_event_base_t event_base,
                                        int32_t event_id,
                                        void* event_data);

void *usr_memset(void *src, int val, size_t size);

int usr_putchar(int c);
int usr_printf(const char *fmt, ...);
int usr_vprintf(const char *fmt, va_list arg);

int usr_xTaskCreatePinnedToCore(TaskFunction_t pvTaskCode,
                                   const char * const pcName,
			                       const uint32_t usStackDepth,
                                   void * const pvParameters,
                                   UBaseType_t uxPriority,
                                   TaskHandle_t * const pvCreatedTask,
                                   const BaseType_t xCoreID);

QueueHandle_t usr_xQueueGenericCreate(uint32_t QueueLength, uint32_t ItemSize, uint8_t ucQueueType);

int usr_xQueueReceive(QueueHandle_t xQueue, void * const buffer, TickType_t TickstoWait);
int usr_xQueueSend(QueueHandle_t xQueue, const void *pvItemToQueue, TickType_t TickstoWait);

void usr_vQueueDelete(QueueHandle_t xQueue);

void usr_vTaskDelay(TickType_t TickstoWait);

void usr_vTaskDelete(TaskHandle_t TaskHandle);

esp_err_t usr_nvs_flash_init();
esp_err_t usr_esp_wifi_init(const wifi_init_config_t *wifi_config);
esp_err_t usr_esp_netif_init();
esp_err_t usr_esp_event_loop_create_default();
esp_netif_t* usr_esp_netif_create_default_wifi_sta();
esp_err_t usr_esp_event_handler_instance_register(usr_esp_event_base_t event_base,
                                              int32_t event_id,
                                              usr_esp_event_handler_t event_handler,
                                              void *event_handler_arg,
                                              usr_esp_event_handler_instance_t *context);
esp_err_t usr_esp_event_handler_instance_unregister(usr_esp_event_base_t event_base,
                                                    int32_t event_id,
                                                    usr_esp_event_handler_instance_t context);
esp_err_t usr_esp_wifi_set_mode(wifi_mode_t mode);
esp_err_t usr_esp_wifi_set_config(wifi_interface_t interface, wifi_config_t *conf);
esp_err_t usr_esp_wifi_start();
esp_err_t usr_esp_wifi_connect();

int usr_getaddrinfo(const char *nodename, const char *servname, const struct addrinfo *hints, struct addrinfo **res, struct addrinfo *res_data);
void usr_freeaddrinfo(struct addrinfo *ai);
int usr_lwip_socket(int domain, int type, int protocol);
int usr_lwip_connect(int s, const struct sockaddr *name, socklen_t namelen);
int usr_write(int s, const void *data, size_t size);
int usr_read(int s, void *mem, size_t len);
int usr_close(int s);

EventGroupHandle_t usr_xEventGroupCreate(void);
EventBits_t usr_xEventGroupSetBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet);
EventBits_t usr_xEventGroupClearBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToClear);
EventBits_t usr_xEventGroupWaitBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToWaitFor, const BaseType_t xClearOnExit, const BaseType_t xWaitForAllBits, TickType_t xTicksToWait);
void usr_vEventGroupDelete(EventGroupHandle_t xEventGroup);

esp_err_t usr_gpio_config(const gpio_config_t *conf);
esp_err_t usr_gpio_install_isr_service(int intr_alloc_flags);
esp_err_t usr_gpio_isr_handler_add(gpio_num_t gpio_num, gpio_isr_t isr_handler, void *args, usr_gpio_handle_t *gpio_handle);
esp_err_t usr_gpio_isr_handler_remove(usr_gpio_handle_t gpio_handle);

esp_tls_t *usr_esp_tls_conn_http_new(const char *url, const esp_tls_cfg_t *cfg);
ssize_t usr_esp_tls_conn_write(esp_tls_t *tls, const void *data, size_t datalen);
ssize_t usr_esp_tls_conn_read(esp_tls_t *tls, void  *data, size_t datalen);
void usr_esp_tls_conn_delete(esp_tls_t *tls);
