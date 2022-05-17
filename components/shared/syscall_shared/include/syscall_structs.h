// Copyright 2022 Espressif Systems (Shanghai) PTE LTD
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

#pragma once

#include "sys/queue.h"
#include "hal/gpio_types.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_event_base.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *stack;
    int stack_size;
    void *task_errno;
    void *task_handle;
} usr_task_ctx_t;

typedef struct {
    void *usr_event_handler;
    void *usr_args;
    void *event_handler_instance;
} usr_context_t;

typedef struct {
    esp_event_base_t event_base;
    int32_t event_id;
    void *event_data;
    usr_context_t usr_context;
} usr_event_args_t;

typedef struct {
    int gpio_num;
    gpio_isr_t usr_isr;
    void *usr_args;
} usr_gpio_args_t;

typedef struct {
    TimerCallbackFunction_t usr_cb;
    void * usr_timer_id;
    TimerHandle_t timerhandle;
} usr_xtimer_context_t;

typedef struct {
    uint64_t alarm;
    uint64_t period:56;
    uint8_t flags;
    union {
        esp_timer_cb_t callback;
        uint32_t event_id;
    };
    void* arg;
#if WITH_PROFILING
    const char* name;
    size_t times_triggered;
    size_t times_armed;
    size_t times_skipped;
    uint64_t total_callback_run_time;
#endif // WITH_PROFILING
    LIST_ENTRY(esp_timer) list_entry;
} usr_esp_timer_handle_t;

typedef enum {
    ESP_SYSCALL_EVENT_GPIO,
    ESP_SYSCALL_EVENT_ESP_TIMER,
    ESP_SYSCALL_EVENT_XTIMER,
    ESP_SYSCALL_EVENT_ESP_EVENT,
} usr_event_t;

typedef union {
    usr_gpio_args_t gpio_args;
    esp_timer_create_args_t esp_timer_args;
    usr_xtimer_context_t xtimer_args;
    usr_event_args_t event_args;
} usr_dispatch_data_t;

typedef struct {
    usr_event_t event;
    usr_dispatch_data_t dispatch_data;
} usr_dispatch_ctx_t;

typedef struct {
    int free_heap_size;
    int largest_free_block;
    int min_free_heap;
} protected_heap_stats_t;

#ifdef __cplusplus
}
#endif
