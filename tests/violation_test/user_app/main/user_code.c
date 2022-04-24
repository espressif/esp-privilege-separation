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

#include "soc/gpio_struct.h"
#include "hal/gpio_types.h"
#include "driver/gpio.h"
#include "hal/gpio_ll.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"

#include "esp_timer.h"

#define BLINK_GPIO      GPIO_NUM_4

extern uint32_t _user_data_start;
extern uint32_t _user_text_start;
extern uint32_t _user_xip_text_start;

void iram_violation_task()
{
    uint32_t start = (uint32_t)&_user_text_start;

    void (*forbidden_iram)(void) = (void(*)(void))(start - 0x1000);

    forbidden_iram();

    while(1) {
        ;
    }
}

void dram_violation_task()
{
    printf("Writing to forbidden region\n");
    uint32_t *forbidden_dram = (&_user_data_start) - 0x10000;

    *forbidden_dram = 0x41414141;

    while(1) {
        ;
    }
}

void rtc_violation_task()
{
    *(uint32_t *)(SOC_RTC_IRAM_LOW + 0x1000) = 0x41414141;

    while(1) {
        ;
    }
}

void pif_violation_task()
{
    gpio_ll_set_level(&GPIO, BLINK_GPIO, 1);

    while (1) {
        ;
    }
}

void flash_icache_violation_task()
{
    uint32_t start = (uint32_t)&_user_xip_text_start;

    void (*forbidden_flash_cache)(void) = (void(*)(void))(start - 0x10000);

    forbidden_flash_cache();

    while(1) {
        ;
    }
}

void semaphore_handle_manipulation_task()
{
    BaseType_t ret;
    ret = xSemaphoreTake((SemaphoreHandle_t)0x1, portMAX_DELAY);
    if (ret != pdPASS) {
        printf("Failed to take semaphore with handle = 0x1\n");
    }
    ret = xSemaphoreGive((SemaphoreHandle_t)0x1);
    if (ret != pdPASS) {
        printf("Failed to give semaphore with handle = 0x1\n");
    }
    SemaphoreHandle_t sem = xSemaphoreCreateBinary();
    if (!sem) {
        printf("Failed to create semaphore\n");
        abort();
    }
    ret = xSemaphoreGive((SemaphoreHandle_t)(((void *)sem) + 4));
    if (ret != pdPASS) {
        printf("Failed to give semaphore with handle = handle + 4\n");
    }
    ret = xSemaphoreGive(sem);
    if (ret != pdPASS) {
        printf("Failed to give semaphore\n");
        abort();
    }
    ret = xSemaphoreTake((SemaphoreHandle_t)(((void *)sem) - 4), portMAX_DELAY);
    if (ret != pdPASS) {
        printf("Failed to take semaphore with handle = handle - 4\n");
    }
    ret = xSemaphoreTake(sem, portMAX_DELAY);
    if (ret != pdPASS) {
        printf("Failed to take semaphore\n");
        abort();
    }
    vSemaphoreDelete((SemaphoreHandle_t)(((void *)sem) - 4));
    vSemaphoreDelete(sem);
    printf("Semaphore handle manipulation test complete\n");
    vTaskDelete(NULL);
}

void task_handle_manipulation_task()
{
    TaskHandle_t handle = xTaskGetCurrentTaskHandle();
    BaseType_t prio = uxTaskPriorityGet(handle - 4);
    if (prio == -1) {
        printf("Failed to get priority with handle = handle - 4\n");
    }
    prio = uxTaskPriorityGet(handle);
    if (prio == -1) {
        printf("Failed to get priority\n");
        abort();
    }

    BaseType_t stack_watermark = uxTaskGetStackHighWaterMark(handle + 1);
    if (stack_watermark == -1) {
        printf("Failed to get stack watermark with handle = handle + 1\n");
    }
    stack_watermark = uxTaskGetStackHighWaterMark(handle);
    if (stack_watermark == -1) {
        printf("Failed to get stack watermark\n");
        abort();
    }

    vTaskDelete(handle - 4);
    vTaskDelete(handle + 4);
    printf("Task handle manipulation test complete\n");
    vTaskDelete(handle);
}

static void esp_timer_callback(void* arg)
{
    printf("ESP-Timer callback triggered\n");
}

void esp_timer_handle_manipulation_task()
{
    const esp_timer_create_args_t esp_timer_args = {
        .callback = &esp_timer_callback,
        .name = "esp-timer"
    };
    esp_timer_handle_t esp_timer_handle;

    esp_err_t err = esp_timer_create(&esp_timer_args, &esp_timer_handle);
    if (err != ESP_OK) {
        printf("Failed to create esp-timer, 0x%x\n", err);
    }

    err = esp_timer_start_periodic((TimerHandle_t)((void *)esp_timer_handle - 4), 500000);
    if (err != ESP_OK) {
        printf("Failed to create esp_timer with handle = handle - 4\n");
    }
    err = esp_timer_start_periodic(esp_timer_handle, 500000);
    if (err != ESP_OK) {
        printf("Failed to create esp_timer, 0x%x\n", err);
        abort();
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    err = esp_timer_stop((TimerHandle_t)((void *)esp_timer_handle - 4));
    if (err != ESP_OK) {
        printf("Failed to stop esp_timer with handle = handle + 4\n");
    }
    err = esp_timer_stop(esp_timer_handle);
    if (err != ESP_OK) {
        printf("Failed to stop esp_timer, 0x%x\n", err);
        abort();
    }

    err = esp_timer_delete((TimerHandle_t)0x1);
    if (err != ESP_OK) {
        printf("Failed to delete esp_timer_handle with handle = 0x1\n");
    }
    err = esp_timer_delete(esp_timer_handle);
    if (err != ESP_OK) {
        printf("Failed to delete esp_timer_handle, 0x%x\n", err);
        abort();
    }

    printf("ESP-Timer handle manipulation test complete\n");
    vTaskDelete(NULL);
}

static void xtimer_callback(TimerHandle_t xTimer)
{
    printf("xTimer callback triggered\n");
}

void xtimer_handle_manipulation_task()
{
    TimerHandle_t xTimer = xTimerCreate("xTimerTest", 500 / portTICK_PERIOD_MS,pdFALSE, NULL, xtimer_callback);
    SemaphoreHandle_t sem = xSemaphoreCreateBinary();
    BaseType_t err = xTimerStart(sem, portMAX_DELAY);
    if (err != pdPASS) {
        printf("Failed to start xTimer for handle = SemaphoreHandle_t\n");
    }
    err = xTimerStart(xTimer, portMAX_DELAY);
    if (err != pdPASS) {
        printf("Failed to start xTimer, 0x%x\n", err);
        abort();
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    err = xTimerStop((xTimer - 4), portMAX_DELAY);
    if (err != pdPASS) {
        printf("Failed to stop xTimer for handle = handle - 4\n");
    }
    err = xTimerStop(xTimer, portMAX_DELAY);
    if (err != pdPASS) {
        printf("Failed to stop xTimer, 0x%x\n", err);
        abort();
    }

    err = xTimerDelete(sem, portMAX_DELAY);
    if (err != pdPASS) {
        printf("Failed to delete xTimer for handle = SemaphoreHandle_t\n");
    }
    err = xTimerDelete(xTimer, portMAX_DELAY);
    if (err != pdPASS) {
        printf("Failed to delete xTimer, 0x%x\n", err);
        abort();
    }
    vSemaphoreDelete(sem);

    printf("xTimer handle manipulation test complete\n");
    vTaskDelete(NULL);
}

void user_main()
{
    printf("Testing violations\n");

    if (xTaskCreate(iram_violation_task, "iram_task", 2048, NULL, 5, NULL) != pdPASS) {
        printf("Task Creation failed\n");
    }

    if (xTaskCreate(dram_violation_task, "dram_task", 2048, NULL, 5, NULL) != pdPASS) {
        printf("Task Creation failed\n");
    }

    if (xTaskCreate(rtc_violation_task, "rtc_task", 2048, NULL, 5, NULL) != pdPASS) {
        printf("Task Creation failed\n");
    }

    if (xTaskCreate(pif_violation_task, "pif_task", 2048, NULL, 5, NULL) != pdPASS) {
        printf("Task Creation failed\n");
    }

    if (xTaskCreate(flash_icache_violation_task, "flash_task", 2048, NULL, 5, NULL) != pdPASS) {
        printf("Task Creation failed\n");
    }
    vTaskDelay(pdMS_TO_TICKS(200));
    if (xTaskCreate(semaphore_handle_manipulation_task, "semaphore_handle_manipulation_task", 2048, NULL, 5, NULL) != pdPASS) {
        printf("Task Creation failed\n");
    }
    vTaskDelay(pdMS_TO_TICKS(200));
    if (xTaskCreate(task_handle_manipulation_task, "task_handle_manipulation_task", 2048, NULL, 5, NULL) != pdPASS) {
        printf("Task Creation failed\n");
    }
    vTaskDelay(pdMS_TO_TICKS(200));
    if (xTaskCreate(esp_timer_handle_manipulation_task, "esp_timer_handle_manipulation_task", 2048, NULL, 5, NULL) != pdPASS) {
        printf("Task Creation failed\n");
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    if (xTaskCreate(xtimer_handle_manipulation_task, "xtimer_handle_manipulation_task", 2048, NULL, 5, NULL) != pdPASS) {
        printf("Task Creation failed\n");
    }
}
