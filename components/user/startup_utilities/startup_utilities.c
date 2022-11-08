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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "string.h"
#include "soc_defs.h"
#include "esp32c3/rom/rom_layout.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_wifi.h"

#include <lwip/sockets.h>
#include <lwip/netdb.h>

#include "esp_heap_caps_init.h"
#include "esp_rom_md5.h"
#include "esp_log.h"

#include "syscall_structs.h"

#ifdef CONFIG_PA_CONSOLE_ENABLE
#include "user_console.h"
#endif

#define CLEANUP_TASK_STACK_SIZE     1024
#define CLEANUP_TASK_PRIO           20
#define DISPATCHER_TASK_PRIO 22

static const char *TAG = "user_startup";

extern int _user_bss_start;
extern int _user_bss_end;
extern int _user_heap_start;
extern int _heap_start;
extern int _user_data_start;
extern void user_main(void);

/* .startup_resources section is placed at the end of .bss section and before heap start.
 *
 * We place usr_resources_t struct here which contains the resources (startup stack, startup errno)
 * required by the protected app to spawn the first user task, as the protected space has no knowledge of user space heap.
 */
const __attribute__((section(".startup_resources"))) usr_resources_t startup_res;

/* Embed _user_data_start, _user_heap_start, and pointer to startup_res as custom_app_desc in the user_app binary.
 * Due to its fixed offset (sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)),
 * it can be parsed by protected app to fetch information required to spawn user app
 */

const __attribute__((section(".rodata_custom_desc"))) usr_custom_app_desc_t custom_app_desc = {
    .user_app_dram_start = (int)&_user_data_start,
    .user_app_heap_start = (int)&_user_heap_start,
    .user_app_resources = (usr_resources_t *)&startup_res
};

/* Queue to receive registered events from protected app */
static QueueHandle_t usr_dispatcher_queue;
/* Task to trigger event callbacks in user app */
static TaskHandle_t usr_dispatcher_task_handle;

/* Queue to receive user space heap pointers from protected app */
static QueueHandle_t usr_mem_cleanup_queue;

static bool _is_heap_initialized;

#if CONFIG_IDF_TARGET_ESP32C3
/* Define all memory regions as default DRAM memory region.
 * Block 9 is defined as DRAM used for startup stack in IDF but in case of user app,
 * we use pre-reserved memory as startup stack
 */

/* A split line is added at ROM_DRAM_RESERVE_START to protect
 * reserved ROM region. As split line can only be added at 256
 * byte aligned address, we have used ALIGN_DOWN macro here.
 */
#define ROM_DRAM_RESERVE_START          ALIGN_DOWN(0x3FCDF060, 256)

const ets_rom_layout_t layout =  {.dram0_rtos_reserved_start = (void *)ROM_DRAM_RESERVE_START};

const ets_rom_layout_t * const ets_rom_layout_p = &layout;

const soc_memory_region_t soc_memory_regions[] = {
    { 0x3FC80000, 0x20000, 0, 0x40380000}, //Block 4,  can be remapped to ROM, can be used as trace memory
    { 0x3FCA0000, 0x20000, 0, 0x403A0000}, //Block 5,  can be remapped to ROM, can be used as trace memory
    { 0x3FCC0000, 0x20000, 0, 0x403C0000}, //Block 9,  can be used as trace memory
#ifdef CONFIG_ESP_SYSTEM_ALLOW_RTC_FAST_MEM_AS_HEAP
    { 0x50000000, 0x2000,  4, 0}, //Fast RTC memory
#endif
};

const size_t soc_memory_region_count = sizeof(soc_memory_regions) / sizeof(soc_memory_region_t);

#elif CONFIG_IDF_TARGET_ESP32S3

/* A split line is added at ROM_DRAM_RESERVE_START to protect
 * reserved ROM region. As split line can only be added at 256
 * byte aligned address, we have used ALIGN_DOWN macro here.
 */
#define ROM_DRAM_RESERVE_START          ALIGN_DOWN(0x3FCEEE34, 256)

const ets_rom_layout_t layout =  {.dram0_rtos_reserved_start = (void *)ROM_DRAM_RESERVE_START};

const ets_rom_layout_t * const ets_rom_layout_p = &layout;

const soc_memory_region_t soc_memory_regions[] = {
/*
#ifdef CONFIG_SPIRAM
    { SOC_EXTRAM_DATA_LOW, SOC_EXTRAM_DATA_SIZE, 4, 0}, //SPI SRAM, if available
#endif
#if CONFIG_ESP32S3_INSTRUCTION_CACHE_16KB
    { 0x40374000, 0x4000,  3, 0},          //Level 1, IRAM
#endif
*/
    { 0x3FC88000, 0x8000,  2, 0x40378000}, //Level 2, IDRAM, can be used as trace memroy
    { 0x3FC90000, 0x10000, 2, 0x40380000}, //Level 3, IDRAM, can be used as trace memroy
    { 0x3FCA0000, 0x10000, 2, 0x40390000}, //Level 4, IDRAM, can be used as trace memroy
    { 0x3FCB0000, 0x10000, 2, 0x403A0000}, //Level 5, IDRAM, can be used as trace memroy
    { 0x3FCC0000, 0x10000, 2, 0x403B0000}, //Level 6, IDRAM, can be used as trace memroy
    { 0x3FCD0000, 0x10000, 2, 0x403C0000}, //Level 7, IDRAM, can be used as trace memroy
    { 0x3FCE0000, 0x10000, 2, 0},          //Level 8, IDRAM, can be used as trace memroy, contains stacks used by startup flow, recycled by heap allocator in app_main task
/*
#if CONFIG_ESP32S3_DATA_CACHE_16KB || CONFIG_ESP32S3_DATA_CACHE_32KB
    { 0x3FCF0000, 0x8000,  0, 0},          //Level 9, DRAM
#endif
#if CONFIG_ESP32S3_DATA_CACHE_16KB
    { 0x3C000000, 0x4000,  5, 0}
#endif
#ifdef CONFIG_ESP_SYSTEM_ALLOW_RTC_FAST_MEM_AS_HEAP
    { 0x600fe000, 0x2000,  6, 0}, //Fast RTC memory
#endif
*/
};

const size_t soc_memory_region_count = sizeof(soc_memory_regions) / sizeof(soc_memory_region_t);

#endif

void usr_vPortCPUInitializeMutex(portMUX_TYPE *mux)
{
    (void)mux;
}

void usr_vPortCPUAcquireMutex(portMUX_TYPE *mux)
{
    (void)mux;
}

esp_err_t usr_clear_bss()
{
    if (is_valid_udram_addr(&_user_bss_start) && is_valid_udram_addr(&_user_bss_end)) {
        memset(&_user_bss_start, 0, (&_user_bss_end - &_user_bss_start) * sizeof(_user_bss_start));
        return 0;
    } else {
        return -1;
    }
}

UIRAM_ATTR static void usr_dispatcher_task(void *arg)
{
    QueueHandle_t usr_queue = (QueueHandle_t)arg;
    usr_dispatch_ctx_t dispatch_ctx;
    while (1) {
        xQueueReceive(usr_queue, &dispatch_ctx, portMAX_DELAY);
        switch (dispatch_ctx.event) {
            case ESP_SYSCALL_EVENT_GPIO: {
                usr_gpio_args_t *usr_context = (usr_gpio_args_t *)&dispatch_ctx.dispatch_data.gpio_args;
                if (is_valid_user_i_addr(usr_context->usr_isr)) {
                    (usr_context->usr_isr)(usr_context->usr_args);
                }
                break;
            }

            case ESP_SYSCALL_EVENT_ESP_TIMER: {
                esp_timer_create_args_t *usr_context = (esp_timer_create_args_t *)&dispatch_ctx.dispatch_data.esp_timer_args;
                if (is_valid_user_i_addr(usr_context->callback)) {
                    (usr_context->callback)(usr_context->arg);
                }
                break;
            }
            case ESP_SYSCALL_EVENT_XTIMER: {
                usr_xtimer_context_t *usr_context = (usr_xtimer_context_t *)&dispatch_ctx.dispatch_data.xtimer_args;
                if (is_valid_user_i_addr(usr_context->usr_cb)) {
                    (usr_context->usr_cb)(usr_context->timerhandle);
                }
                break;
            }
            case ESP_SYSCALL_EVENT_ESP_EVENT: {
                usr_event_args_t *event_args = (usr_event_args_t *)&dispatch_ctx.dispatch_data.event_args;
                esp_event_handler_t usr_event_handler = event_args->usr_context.usr_event_handler;
                if (usr_event_handler) {
                    (*usr_event_handler)(event_args->usr_context.usr_args, event_args->event_base, event_args->event_id, event_args->event_data);
                }
                break;
            }
            default:
                break;
        }
    }
}

/* This task is responsible for freeing up user stack and user errno variable used for a user task.
 * Since protected space has no knowledge of user heap, it sends the user space stack pointer and
 * user space errno variable on the queue for it to be freed and reclaimed by the user space.
 */
void usr_mem_cleanup_task(void *args)
{
    void *ptr;
    QueueHandle_t queue = (QueueHandle_t)args;
    while(1) {
        xQueueReceive(queue, &ptr, portMAX_DELAY);
        if (ptr == &startup_res.startup_stack) {
            /* The first user task is spawned by the protected app and it uses the memory reserved
             * in .startup_resources section. This memory is not added in the heap initially and when
             * the first user task is deleted, we add the memory into heap
             */
            heap_caps_add_region((intptr_t)ptr, (intptr_t)&_heap_start);
        } else if (ptr > (void *)&_heap_start) {
            free(ptr);
        }
    }
}

static void user_cleanup_service_init()
{
    /* Create queue which will receive user space memory that needs to be freed.
     * queueQUEUE_TYPE_CLEANUP is a special queue type defined by esp_priv_access component.
     * It will be cached in protected space and used to send data on.
     */
    usr_mem_cleanup_queue = xQueueGenericCreate(20, sizeof(void *), queueQUEUE_TYPE_CLEANUP);
    if (usr_mem_cleanup_queue == NULL) {
        ESP_LOGE(TAG, "Error creating cleanup queue\nAborting...");
        abort();
    }

    // Create clean up task with a higher priority to ensure that the user memory is freed instantenously
    int ret = xTaskCreate(usr_mem_cleanup_task, "User cleanup task", CLEANUP_TASK_STACK_SIZE, usr_mem_cleanup_queue, CLEANUP_TASK_PRIO, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Error creating cleanup task\nAborting...");
        abort();
    }
}

static void user_dispatch_service_init()
{
    usr_dispatcher_queue = xQueueGenericCreate(20, sizeof(usr_dispatch_ctx_t), queueQUEUE_TYPE_DISPATCH);
    if (!usr_dispatcher_queue) {
        ESP_LOGE(TAG, "Failed to create dispatcher queue\nAborting...");
        abort();
    }
    xTaskCreate(usr_dispatcher_task, "User event dispatcher", CONFIG_PA_USER_DISPATCHER_TASK_STACK_SIZE, usr_dispatcher_queue, DISPATCHER_TASK_PRIO, &usr_dispatcher_task_handle);
    if (!usr_dispatcher_task_handle) {
        ESP_LOGE(TAG, "Failed to create dispatcher task\nAborting...");
        abort();
    }
}

void _user_main()
{
    usr_clear_bss();
    heap_caps_init();
    _is_heap_initialized = 1;
    user_cleanup_service_init();
    user_dispatch_service_init();
#ifdef CONFIG_PA_CONSOLE_ENABLE
    user_console_init();
#endif
    user_main();
    vTaskDelete(NULL);
}
