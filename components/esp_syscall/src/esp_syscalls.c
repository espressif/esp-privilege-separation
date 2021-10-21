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
#include "string.h"
#include <syscall_def.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "hal/gpio_types.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include <nvs_flash.h>
#include "syscall_wrappers.h"
#include "syscall_priv.h"

#include <lwip/sockets.h>
#include <lwip/netdb.h>

#include "esp_tls.h"

#include "soc_defs.h"

#if CONFIG_IDF_TARGET_ARCH_RISCV
#include "riscv/rvruntime-frames.h"
#include "ecall_context.h"
#endif

#define TAG     __func__

typedef void (*syscall_t)(void);

static DRAM_ATTR QueueHandle_t usr_gpio_isr_queue;
static DRAM_ATTR QueueHandle_t usr_event_loop_queue;

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

static int sys_ni_syscall(void)
{
    return -1;
}

static int sys_putchar(int c)
{
    return putchar(c);
}

static int sys_vprintf(const char *format, va_list arg)
{
    return vprintf(format, arg);
};

static int sys_xTaskCreate(TaskFunction_t pvTaskCode,
                                   const char * const pcName,
			                       const uint32_t usStackDepth,
                                   void * const pvParameters,
                                   UBaseType_t uxPriority,
                                   TaskHandle_t * const pvCreatedTask)
{
    if (!is_valid_user_i_addr(pvTaskCode)) {
        ESP_LOGE(TAG, "Incorrect address of user function");
        return pdFAIL;
    }

    TaskHandle_t handle;
    StaticTask_t *xtaskTCB = heap_caps_malloc(sizeof(StaticTask_t), portTcbMemoryCaps);
    if (xtaskTCB == NULL) {
        ESP_LOGE(TAG, "Insufficient memory for TCB");
        return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
    }
    StackType_t *xtaskStack = heap_caps_malloc(usStackDepth, MALLOC_CAP_WORLD1);
    if (xtaskStack == NULL) {
        ESP_LOGE(TAG, "Insufficient memory for user stack");
        free(xtaskTCB);
        return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
    }
    StackType_t *kernel_stack = heap_caps_malloc(KERNEL_STACK_SIZE, portStackMemoryCaps);
    if (kernel_stack == NULL) {
        ESP_LOGE(TAG, "Insufficient memory for kernel stack");
        free(xtaskTCB);
        free(xtaskStack);
        return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
    }
    memset(kernel_stack, tskSTACK_FILL_BYTE, KERNEL_STACK_SIZE * sizeof( StackType_t ) );

    /* Suspend the scheduler to ensure that it does not switch to the newly created task.
     * We need to first set the kernel stack as a TLS and only then it should be executed
     */
    vTaskSuspendAll();
    handle = xTaskCreateStatic(pvTaskCode, pcName, usStackDepth, pvParameters, uxPriority, xtaskStack, xtaskTCB);

    vTaskSetThreadLocalStoragePointerAndDelCallback(handle, 1, kernel_stack, NULL);
    vTaskSetThreadLocalStoragePointerAndDelCallback(handle, 2, (void *)1, NULL);
    xTaskResumeAll();

    if (pvCreatedTask) {
        *pvCreatedTask = handle;
    }
    return pdPASS;
}

static QueueHandle_t sys_xQueueGenericCreate(uint32_t QueueLength, uint32_t ItemSize, uint8_t ucQueueType)
{
    QueueHandle_t q;
    size_t queue_size = (size_t) (QueueLength * ItemSize);
    StaticQueue_t *queue = heap_caps_malloc(sizeof(StaticQueue_t), MALLOC_CAP_DEFAULT);
    if (queue == NULL) {
        ESP_LOGE(TAG, "Insufficient memory for queue struct");
        return NULL;
    }
    uint8_t *queue_storage = heap_caps_malloc(queue_size, MALLOC_CAP_WORLD1);
    q = xQueueGenericCreateStatic(QueueLength, ItemSize, queue_storage, queue, ucQueueType);
    return q;
}

static int sys_xQueueReceive(QueueHandle_t xQueue, void * const buffer, TickType_t TickstoWait)
{
    if (is_valid_kernel_d_addr(xQueue) && is_valid_user_d_addr(buffer)) {
        return xQueueReceive(xQueue, buffer, TickstoWait);
    }
    return -1;
}

static int sys_xQueueSend(QueueHandle_t xQueue, const void *pvItemToQueue, TickType_t TickstoWait)
{
    if (is_valid_kernel_d_addr(xQueue) && is_valid_user_d_addr((void *)pvItemToQueue)) {
        return xQueueSend(xQueue, pvItemToQueue, TickstoWait);
    }
    return -1;
}

static void sys_vQueueDelete(QueueHandle_t xQueue)
{
    if (is_valid_kernel_d_addr(xQueue)) {
        vQueueDelete(xQueue);
        free(((StaticQueue_t *)xQueue)->pvDummy1[0]);
        free(xQueue);
    }
}

static void sys_vTaskDelay(TickType_t TickstoWait)
{
    vTaskDelay(TickstoWait);
    return;
}


void vPortCleanUpTCB ( void *pxTCB )
{
    if (pvTaskGetThreadLocalStoragePointer(pxTCB, 1) == NULL) {
        /* This means this task is a kernel task.
         * User task has a kernel stack at this index
         * Do nothing and return*/
        return;
    }

    // Has to be some other way to free the allocated memory
    if (is_valid_user_d_addr(((StaticTask_t *)pxTCB)->pxDummy6)) {
        /* The stack is the user space stack. Free the kernel space stack */
        void *k_stack = pvTaskGetThreadLocalStoragePointer(pxTCB, 1);
        free(k_stack);
    } else {
        /* The task might be deleted when it is executing system-call, in that case, the stack point will point to kernel stack.
         * Retrieve the user stack from system call stack frame
         */
#if CONFIG_IDF_TARGET_ARCH_XTENSA
        XtSyscallExcFrame *syscall_stack = (XtSyscallExcFrame *)((uint32_t)pxTaskGetStackStart(pxTCB) + KERNEL_STACK_SIZE - XT_ISTK_FRMSZ);
        free((void *)syscall_stack->user_stack);
#elif CONFIG_IDF_TARGET_ARCH_RISCV
        RvEcallFrame *syscall_stack = (RvEcallFrame *)((uint32_t)pxTaskGetStackStart(pxTCB) + KERNEL_STACK_SIZE - RV_ESTK_FRMSZ);
        free((void *)syscall_stack->stack);
#endif
    }
    /* prvDeleteTCB accesses TCB members after this function returns so to avoid use-after-free case,
     * change the ucStaticallyAllocated field such that prvDeleteTCB will free the stack and TCB
     */
    ((StaticTask_t *)pxTCB)->uxDummy20 = 0;
}

static void sys_vTaskDelete(TaskHandle_t TaskHandle)
{
    return vTaskDelete(TaskHandle);
}


IRAM_ATTR static void sys_gpio_isr_handler(void *args)
{
    int need_yield;
    usr_gpio_args_t *usr_context = (usr_gpio_args_t *)args;
    if (xQueueSendFromISR(usr_gpio_isr_queue, usr_context, &need_yield) != pdPASS) {
        return;
    }
    if (need_yield == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static esp_err_t sys_gpio_config(const gpio_config_t *conf)
{
    if (is_valid_user_d_addr((void *)conf)) {
        return gpio_config(conf);
    }

    return ESP_ERR_INVALID_ARG;
}

static esp_err_t sys_gpio_install_isr_service(int intr_alloc_flags)
{
    return gpio_install_isr_service(intr_alloc_flags);
}

static esp_err_t sys_gpio_isr_handler_add(gpio_num_t gpio_num, gpio_isr_t isr_handler, void *args, usr_gpio_handle_t *gpio_handle, QueueHandle_t usr_queue)
{
    esp_err_t ret;
    if (!(is_valid_user_d_addr(gpio_handle) && usr_queue)) {
        ESP_LOGE(TAG, "Incorrect address space for gpio handle or user queue");
        return ESP_ERR_INVALID_ARG;
    }

    usr_gpio_isr_queue = usr_queue;

    usr_gpio_args_t *usr_context = heap_caps_malloc(sizeof(usr_gpio_args_t), MALLOC_CAP_WORLD1);
    if (!usr_context) {
        ESP_LOGE(TAG, "Insufficient memory for user context");
        return ESP_ERR_NO_MEM;
    }
    usr_context->gpio_num = gpio_num;
    usr_context->usr_isr = isr_handler;
    usr_context->usr_args = args;
    ret = gpio_isr_handler_add(gpio_num, sys_gpio_isr_handler, (void *)usr_context);
    if (ret != ESP_OK) {
        free(usr_context);
    } else {
        *gpio_handle = usr_context;
    }
    return ret;
}

static esp_err_t sys_gpio_isr_handler_remove(usr_gpio_handle_t gpio_handle)
{
    esp_err_t ret;
    usr_gpio_args_t *usr_context = (usr_gpio_args_t *)gpio_handle;
    if (usr_context && is_valid_user_d_addr(usr_context)) {
        ret = gpio_isr_handler_remove(usr_context->gpio_num);
        if (ret == ESP_OK) {
            free(usr_context);
        }
        return ret;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
}

static esp_err_t sys_nvs_flash_init()
{
    return nvs_flash_init();
}

static esp_err_t sys_esp_netif_init()
{
    return esp_netif_init();
}

static esp_err_t sys_esp_event_loop_create_default()
{
    return esp_event_loop_create_default();
}

static esp_netif_t* sys_esp_netif_create_default_wifi_sta()
{
    return esp_netif_create_default_wifi_sta();
}

static void sys_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    usr_event_args_t usr_event;

    if (event_base == WIFI_EVENT) {
        usr_event.event_base = WIFI_EVENT_BASE;
    } else if (event_base == IP_EVENT) {
        usr_event.event_base = IP_EVENT_BASE;
    } else {
        usr_event.event_base = -1;
    }

    usr_event.event_id = event_id;
    usr_event.event_data = event_data;
    usr_event.usr_context = (usr_context_t *)arg;

    if (xQueueSend(usr_event_loop_queue, &usr_event, portMAX_DELAY) == pdFALSE) {
        printf("Error sending message on queue\n");
    }
    return;
}

static esp_err_t sys_esp_event_handler_instance_register(usr_esp_event_base_t event_base,
                                              int32_t event_id,
                                              esp_event_handler_t event_handler,
                                              void *event_handler_arg,
                                              usr_esp_event_handler_instance_t *context,
                                              QueueHandle_t usr_queue)
{
    esp_err_t ret;
    esp_event_base_t sys_event_base;
    switch(event_base) {
        case WIFI_EVENT_BASE:
            sys_event_base = WIFI_EVENT;
            break;
        case IP_EVENT_BASE:
            sys_event_base = IP_EVENT;
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    if (!(is_valid_user_d_addr(context) && usr_queue)) {
        ESP_LOGE(TAG, "Incorrect address space for context or user_queue");
        return ESP_ERR_INVALID_ARG;
    }

    usr_event_loop_queue = usr_queue;

    usr_context_t *usr_context = heap_caps_malloc(sizeof(usr_context_t), MALLOC_CAP_WORLD1);
    if (!usr_context) {
        ESP_LOGE(TAG, "Insufficient memory for user context");
        return ESP_ERR_NO_MEM;
    }
    usr_context->usr_event_handler = event_handler;
    usr_context->usr_args = event_handler_arg;
    ret = esp_event_handler_instance_register(sys_event_base, event_id, &sys_event_handler, (void *)usr_context, &(usr_context->event_handler_instance));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register event handler");
        free(usr_context);
    } else {
        *context = usr_context;
    }
    return ret;
}

static esp_err_t sys_esp_event_handler_instance_unregister(usr_esp_event_base_t event_base,
                                                int32_t event_id,
                                                usr_esp_event_handler_instance_t context)
{
    esp_err_t ret;
    esp_event_base_t sys_event_base;
    switch(event_base) {
        case WIFI_EVENT_BASE:
            sys_event_base = WIFI_EVENT;
            break;
        case IP_EVENT_BASE:
            sys_event_base = IP_EVENT;
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    usr_context_t *usr_context = (usr_context_t *)context;
    if (usr_context && is_valid_user_d_addr(usr_context)) {
        ret = esp_event_handler_instance_unregister(sys_event_base, event_id, usr_context->event_handler_instance);
        if (ret == ESP_OK) {
            free(usr_context);
        }
        return ret;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
}

static esp_err_t sys_esp_wifi_init(const wifi_init_config_t *config)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    return esp_wifi_init(&cfg);
}

static esp_err_t sys_esp_wifi_set_mode(wifi_mode_t mode)
{
    return esp_wifi_set_mode(mode);
}

static esp_err_t sys_esp_wifi_set_config(wifi_interface_t interface, wifi_config_t *conf)
{
    if (is_valid_user_d_addr(conf)) {
        return esp_wifi_set_config(interface, conf);
    }
    return -1;
}

static esp_err_t sys_esp_wifi_start()
{
    return esp_wifi_start();
}

static esp_err_t sys_esp_wifi_connect()
{
    return esp_wifi_connect();
}

static int sys_getaddrinfo(const char *nodename, const char *servname,
       const struct addrinfo *hints, struct addrinfo **res)
{
    int ret;
    struct addrinfo *tmp_res;
    ret = getaddrinfo(nodename, servname, hints, &tmp_res);
    if (ret == 0) {
        struct addrinfo *usr_res = heap_caps_malloc(NETDB_ELEM_SIZE, MALLOC_CAP_WORLD1);
        if (!usr_res) {
            freeaddrinfo(tmp_res);
            return -1;
        }
        memcpy(usr_res, tmp_res, NETDB_ELEM_SIZE);
        if (tmp_res->ai_next) {
            ESP_LOGW(TAG, "More nodes in linked list..skipping");
            usr_res->ai_next = NULL;
        }
        // Correct the location of ai_addr wrt user pointer. Data is already copied above by memcpy
        usr_res->ai_addr = (struct sockaddr *)(void *)((u8_t *)usr_res + sizeof(struct addrinfo));

        // Correct the location of nodename wrt user pointer. Data is already copied above by memcpy
        usr_res->ai_canonname = ((char *)usr_res + sizeof(struct addrinfo) + sizeof(struct sockaddr_storage));

        *res = usr_res;
        freeaddrinfo(tmp_res);
    }
    return ret;
}

static void sys_freeaddrinfo(struct addrinfo *ai)
{
    if(is_valid_user_d_addr((void *)ai)) {
        free(ai);
    }
}

static int sys_socket(int domain, int type, int protocol)
{
    return socket(domain, type, protocol);
}

static int sys_connect(int s, const struct sockaddr *name, socklen_t namelen)
{
    return connect(s, name, namelen);
}

static int sys_write(int s, const void *data, size_t size)
{
    if (is_valid_user_d_addr((void *)data)) {
        return write(s, data, size);
    }
    return -1;
}

static int sys_read(int s, void *mem, size_t len)
{
    if (is_valid_user_d_addr(mem)) {
        return read(s, mem, len);
    }
    return -1;
}

static int sys_close(int s)
{
    return close(s);
}

static EventGroupHandle_t sys_xEventGroupCreate(void)
{
    return xEventGroupCreate();
}

static EventBits_t sys_xEventGroupSetBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet)
{
    return xEventGroupSetBits(xEventGroup, uxBitsToSet);
}

static EventBits_t sys_xEventGroupClearBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToClear)
{
    return xEventGroupClearBits(xEventGroup, uxBitsToClear);
}

static EventBits_t sys_xEventGroupWaitBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToWaitFor, const BaseType_t xClearOnExit, const BaseType_t xWaitForAllBits, TickType_t xTicksToWait)
{
    return xEventGroupWaitBits(xEventGroup, uxBitsToWaitFor, xClearOnExit, xWaitForAllBits, xTicksToWait);
}

static void sys_vEventGroupDelete(EventGroupHandle_t xEventGroup)
{
    vEventGroupDelete(xEventGroup);
    return;
}

static esp_tls_t *sys_esp_tls_conn_http_new(const char *url, const esp_tls_cfg_t *cfg)
{
    if (is_valid_user_d_addr((void *)cfg)) {
        return esp_tls_conn_http_new(url, cfg);
    }
    return NULL;
}

static ssize_t sys_esp_tls_conn_write(esp_tls_t *tls, const void *data, size_t datalen)
{
    if (is_valid_kernel_d_addr((void *)tls) && is_valid_user_d_addr((void *)data)) {
        ssize_t len = esp_tls_conn_write(tls, data, datalen);
        return len;
    }
    return -1;
}

static ssize_t sys_esp_tls_conn_read(esp_tls_t *tls, void  *data, size_t datalen)
{
    if (is_valid_kernel_d_addr(tls) && is_valid_user_d_addr(data)) {
        return esp_tls_conn_read(tls, data, datalen);
    }
    return -1;
}

static void sys_esp_tls_conn_delete(esp_tls_t *tls)
{
    if (is_valid_kernel_d_addr(tls)) {
        esp_tls_conn_delete(tls);
    }
    return;
}

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
#endif
DRAM_ATTR syscall_t syscall_table[__NR_syscalls] = {
    [0 ... __NR_syscalls - 1] = (syscall_t)&sys_ni_syscall,

#define __SYSCALL(nr, symbol, nargs)  [nr] = (syscall_t)symbol,
#include "esp_syscall.h"
};
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
