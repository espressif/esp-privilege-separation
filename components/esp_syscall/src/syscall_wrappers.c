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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "string.h"
#include "syscall_def.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "syscall_wrappers.h"
#include "syscall_priv.h"
#include "esp_event.h"
#include "esp_wifi.h"

#include <lwip/sockets.h>
#include <lwip/netdb.h>

#include "esp_tls.h"

#ifndef XTSTR
#define _XTSTR(x)	# x
#define XTSTR(x)	_XTSTR(x)
#endif

extern int _user_bss_start;
extern int _user_bss_end;
extern void user_main(void);

static QueueHandle_t usr_gpio_isr_queue;
static QueueHandle_t usr_event_loop_queue;
static TaskHandle_t usr_event_loop_task_handle;
static TaskHandle_t usr_gpio_isr_task_handle;

static uint8_t _event_handler_count;
static uint8_t _gpio_handler_count;

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wint-conversion"
#endif

/* Reserve 256 bytes for user app descriptor required by esptool while generating user app binary.
 * esptool inserts SHA256 at 144th offset, so it needs to be all zeros
 */
const __attribute__((section(".rodata_desc"))) uint8_t user_app_desc[256];

int usr_xTaskCreatePinnedToCore(TaskFunction_t pvTaskCode,
                                   const char * const pcName,
			                       const uint32_t usStackDepth,
                                   void * const pvParameters,
                                   UBaseType_t uxPriority,
                                   TaskHandle_t * const pvCreatedTask,
                                   const BaseType_t xCoreID)
{
    // Not using the xCoreID argument as this is a single core system and we don't support more than 6 arguments
    return EXECUTE_SYSCALL(pvTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pvCreatedTask, __NR_xTaskCreatePinnedToCore);
}

QueueHandle_t usr_xQueueGenericCreate(uint32_t QueueLength, uint32_t ItemSize, uint8_t ucQueueType)
{
    return EXECUTE_SYSCALL(QueueLength, ItemSize, ucQueueType, __NR_xQueueGenericCreate);
}

int usr_xQueueReceive(QueueHandle_t xQueue, void * const buffer, TickType_t TickstoWait)
{
    return EXECUTE_SYSCALL(xQueue, buffer, TickstoWait, __NR_xQueueReceive);
}

int usr_xQueueSend(QueueHandle_t xQueue, const void *pvItemToQueue, TickType_t TickstoWait)
{
    return EXECUTE_SYSCALL(xQueue, pvItemToQueue, TickstoWait, __NR_xQueueSend);
}

void usr_vQueueDelete(QueueHandle_t xQueue)
{
    EXECUTE_SYSCALL(xQueue, __NR_vQueueDelete);
}

void usr_vTaskDelay(TickType_t TickstoWait)
{
    EXECUTE_SYSCALL(TickstoWait, __NR_vTaskDelay);
}

void usr_vTaskDelete(TaskHandle_t TaskHandle)
{
    EXECUTE_SYSCALL(TaskHandle, __NR_vTaskDelete);
}

esp_err_t usr_gpio_config(const gpio_config_t *conf)
{
    return EXECUTE_SYSCALL(conf, __NR_gpio_config);
}

esp_err_t usr_gpio_install_isr_service(int intr_alloc_flags)
{
    return EXECUTE_SYSCALL(intr_alloc_flags, __NR_gpio_install_isr_service);
}

UIRAM_ATTR static void usr_gpio_isr_task(void *arg)
{
    QueueHandle_t usr_queue = (QueueHandle_t)arg;
    usr_gpio_args_t usr_context;
    while (1) {
        usr_xQueueReceive(usr_queue, &usr_context, portMAX_DELAY);
        if (is_valid_user_i_addr(usr_context.usr_isr)) {
            (usr_context.usr_isr)(usr_context.usr_args);
        }
    }
}

esp_err_t usr_gpio_isr_handler_add(gpio_num_t gpio_num, gpio_isr_t isr_handler, void *args, usr_gpio_handle_t *gpio_handle)
{
    esp_err_t ret;
    _gpio_handler_count++;
    if (_gpio_handler_count == 1) {
        usr_gpio_isr_queue = usr_xQueueGenericCreate(10, sizeof(usr_gpio_args_t), queueQUEUE_TYPE_BASE);
        usr_xTaskCreatePinnedToCore(usr_gpio_isr_task, "User GPIO ISR dispatcher", 1024, usr_gpio_isr_queue, 22, &usr_gpio_isr_task_handle, 0);
    }

    ret = EXECUTE_SYSCALL(gpio_num, isr_handler, args, gpio_handle, usr_gpio_isr_queue, __NR_gpio_isr_handler_add);

    if (ret != 0) {
        usr_vTaskDelete(usr_gpio_isr_task_handle);
        usr_vQueueDelete(usr_gpio_isr_queue);
        _gpio_handler_count--;
    }
    return ret;
}

esp_err_t usr_gpio_isr_handler_remove(usr_gpio_handle_t gpio_handle)
{
    _gpio_handler_count--;
    if (_gpio_handler_count == 0) {
        usr_vTaskDelete(usr_gpio_isr_task_handle);
        usr_vQueueDelete(usr_gpio_isr_queue);
    }
    return EXECUTE_SYSCALL(gpio_handle, __NR_gpio_isr_handler_remove);
}

int usr_putchar(int c)
{
    return EXECUTE_SYSCALL(c, __NR_putchar);
}

int usr_vprintf(const char *format, va_list arg)
{
    return EXECUTE_SYSCALL(format, arg, __NR_vprintf);
}

int usr_printf(const char *fmt, ...)
{
    int ret;
    va_list ap = 0;
    va_start(ap, fmt);
    ret = usr_vprintf(fmt, ap);
    va_end(ap);
    return ret;
}

esp_err_t usr_nvs_flash_init()
{
    return EXECUTE_SYSCALL(__NR_nvs_flash_init);
}

esp_err_t usr_esp_netif_init()
{
    return EXECUTE_SYSCALL(__NR_esp_netif_init);
}

esp_err_t usr_esp_event_loop_create_default()
{
    return EXECUTE_SYSCALL(__NR_esp_event_loop_create_default);
}

esp_netif_t* usr_esp_netif_create_default_wifi_sta()
{
    return EXECUTE_SYSCALL(__NR_esp_netif_create_default_wifi_sta);
}

UIRAM_ATTR void usr_event_loop_task(void *arg)
{
    QueueHandle_t usr_queue = (QueueHandle_t)arg;
    usr_event_args_t usr_event;
    while(1) {
        usr_xQueueReceive(usr_queue, &usr_event, portMAX_DELAY);
        usr_esp_event_handler_t usr_event_handler = (usr_esp_event_handler_t)usr_event.usr_context->usr_event_handler;
        if (usr_event_handler) {
            (*usr_event_handler)(usr_event.usr_context->usr_args, usr_event.event_base, usr_event.event_id, usr_event.event_data);
        }
    }
}

esp_err_t usr_esp_event_handler_instance_register(usr_esp_event_base_t event_base,
                                              int32_t event_id,
                                              usr_esp_event_handler_t event_handler,
                                              void *event_handler_arg,
                                              usr_esp_event_handler_instance_t *context)
{
    esp_err_t ret;
    _event_handler_count++;
    if (_event_handler_count == 1) {
        usr_event_loop_queue = usr_xQueueGenericCreate(10, sizeof(usr_event_args_t), queueQUEUE_TYPE_BASE);
        usr_xTaskCreatePinnedToCore(usr_event_loop_task, "User event loop task", 2048, usr_event_loop_queue, 21, &usr_event_loop_task_handle, 0);
    }

    ret = EXECUTE_SYSCALL(event_base, event_id, event_handler, event_handler_arg, context, usr_event_loop_queue,
                          __NR_esp_event_handler_instance_register);

    if (ret != 0) {
        usr_vTaskDelete(usr_event_loop_task_handle);
        usr_vQueueDelete(usr_event_loop_queue);
        _event_handler_count--;
    }

    return 0;
}

esp_err_t usr_esp_event_handler_instance_unregister(usr_esp_event_base_t event_base,
                                                int32_t event_id,
                                                usr_esp_event_handler_instance_t context)
{
    _event_handler_count--;
    if (_event_handler_count == 0) {
        usr_vTaskDelete(usr_event_loop_task_handle);
        usr_vQueueDelete(usr_event_loop_queue);
    }
    return EXECUTE_SYSCALL(event_base, event_id, context, __NR_esp_event_handler_instance_unregister);
}

esp_err_t usr_esp_wifi_init(const wifi_init_config_t *wifi_config)
{
    return EXECUTE_SYSCALL(wifi_config, __NR_esp_wifi_init);
}

esp_err_t usr_esp_wifi_set_mode(wifi_mode_t mode)
{
    return EXECUTE_SYSCALL(mode, __NR_esp_wifi_set_mode);
}

esp_err_t usr_esp_wifi_set_config(wifi_interface_t interface, wifi_config_t *conf)
{
    return EXECUTE_SYSCALL(interface, conf, __NR_esp_wifi_set_config);
}

esp_err_t usr_esp_wifi_start()
{
    return EXECUTE_SYSCALL(__NR_esp_wifi_start);
}

esp_err_t usr_esp_wifi_connect()
{
    return EXECUTE_SYSCALL(__NR_esp_wifi_connect);
}

int usr_lwip_getaddrinfo(const char *nodename, const char *servname, const struct addrinfo *hints, struct addrinfo **res)
{
    return EXECUTE_SYSCALL(nodename, servname, hints, res, __NR_lwip_getaddrinfo);
}

void usr_lwip_freeaddrinfo(struct addrinfo *ai)
{
    EXECUTE_SYSCALL(ai, __NR_lwip_freeaddrinfo);
}

int usr_lwip_socket(int domain, int type, int protocol)
{
    return EXECUTE_SYSCALL(domain, type, protocol, __NR_lwip_socket);
}

int usr_lwip_connect(int s, const struct sockaddr *name, socklen_t namelen)
{
    return EXECUTE_SYSCALL(s, name, namelen, __NR_lwip_connect);
}

int usr_write(int s, const void *data, size_t size)
{
    return EXECUTE_SYSCALL(s, data, size, __NR_write);
}

int usr_read(int s, void *mem, size_t len)
{
    return EXECUTE_SYSCALL(s, mem, len, __NR_read);
}

int usr_close(int s)
{
    return EXECUTE_SYSCALL(s, __NR_close);
}

EventGroupHandle_t usr_xEventGroupCreate(void)
{
    return EXECUTE_SYSCALL(__NR_xEventGroupCreate);
}

EventBits_t usr_xEventGroupSetBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet)
{
    return EXECUTE_SYSCALL(xEventGroup, uxBitsToSet, __NR_xEventGroupSetBits);
}

EventBits_t usr_xEventGroupClearBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToClear)
{
    return EXECUTE_SYSCALL(xEventGroup, uxBitsToClear, __NR_xEventGroupClearBits);
}

EventBits_t usr_xEventGroupWaitBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToWaitFor, const BaseType_t xClearOnExit, const BaseType_t xWaitForAllBits, TickType_t xTicksToWait)
{
    return EXECUTE_SYSCALL(xEventGroup, uxBitsToWaitFor, xClearOnExit, xWaitForAllBits, xTicksToWait, __NR_xEventGroupWaitBits);
}

void usr_vEventGroupDelete(EventGroupHandle_t xEventGroup)
{
    EXECUTE_SYSCALL(xEventGroup, __NR_vEventGroupDelete);
}

esp_tls_t *usr_esp_tls_conn_http_new(const char *url, const esp_tls_cfg_t *cfg)
{
    return EXECUTE_SYSCALL(url, cfg, __NR_esp_tls_conn_http_new);
}

ssize_t usr_esp_tls_conn_write(esp_tls_t *tls, const void *data, size_t datalen)
{
    return EXECUTE_SYSCALL(tls, data, datalen, __NR_esp_tls_conn_write);
}

ssize_t usr_esp_tls_conn_read(esp_tls_t *tls, void  *data, size_t datalen)
{
    return EXECUTE_SYSCALL(tls, data, datalen, __NR_esp_tls_conn_read);
}

void usr_esp_tls_conn_delete(esp_tls_t *tls)
{
    EXECUTE_SYSCALL(tls, __NR_esp_tls_conn_delete);
}

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

esp_err_t usr_clear_bss()
{
    if ((&_user_bss_start >= (int *)SOC_UDRAM_LOW && &_user_bss_start < (int *)SOC_UDRAM_HIGH && &_user_bss_end < (int *)SOC_UDRAM_HIGH)) {
        memset(&_user_bss_start, 0, (&_user_bss_end - &_user_bss_start) * sizeof(_user_bss_start));
        return 0;
    } else {
        return -1;
    }
}

void _user_main()
{
    usr_clear_bss();
    user_main();
    usr_vTaskDelete(NULL);
}
