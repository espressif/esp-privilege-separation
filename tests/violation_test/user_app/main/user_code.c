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

#ifdef CONFIG_IDF_TARGET_ESP32C3
#include "esp32c3/rom/rom_layout.h"
#elif CONFIG_IDF_TARGET_ESP32S3
#include "esp32s3/rom/rom_layout.h"
#endif

#define BLINK_GPIO      GPIO_NUM_4

extern uint32_t _user_data_start;
extern uint32_t _user_text_start;
extern uint32_t _user_text_end;
extern int _reserve_w1_iram_end;
extern uint32_t _user_xip_text_start;

void irom_violation_task()
{
    void (*rom_printf)(const char *fmt, ...) = (void(*)(const char *fmt, ...))0x40000040;

    rom_printf("Should not print\n");

    while(1) {
        ;
    }
}

void sram_icache_write_violation_task()
{
    uint32_t *start = (uint32_t *)((uint32_t)(&_user_text_start) - 0x100);

    *start = 0x41414141;

    // Unreachable code as the task will be deleted after
    // accessing SRAM icache region.
    while(1) {
        ;
    }
}

void sram_icache_execute_violation_task()
{
    void (*forbidden_icache)(void) = (void(*)(void))(&_user_text_start) - 0x100;

    forbidden_icache();

    // Unreachable code as the task will be deleted after
    // accessing SRAM icache region.
    while(1) {
        ;
    }
}

void iram_execute_violation_task()
{
    uint32_t end = (uint32_t)&_reserve_w1_iram_end;

    void (*forbidden_iram)(void) = (void(*)(void))(end + 0x1000);

    forbidden_iram();

    while(1) {
        ;
    }
}

void iram_write_violation_task()
{
    uint32_t end = (uint32_t)&_reserve_w1_iram_end;

    uint32_t *forbidden_iram = (uint32_t *)(end + 0x1000);

    *forbidden_iram = 0x41414141;

    while(1) {
        ;
    }
}

void iram_read_violation_task()
{
    uint32_t end = (uint32_t)&_reserve_w1_iram_end;

    uint32_t *forbidden_iram = (uint32_t *)(end + 0x1000);

    printf("Value : 0x%x\n", *forbidden_iram);

    while(1) {
        ;
    }
}

void drom_violation_task()
{
    printf("Reading from forbidden DROM region\n");

    uint32_t *forbidden_drom = (uint32_t *)0x3FF10000;

    printf("Value : 0x%x\n", *forbidden_drom);

    while(1) {
        ;
    }
}

void dram_write_violation_task()
{
    printf("Writing to forbidden DRAM region\n");

    uint32_t *forbidden_dram = (&_user_data_start) - 0x10000;

    *forbidden_dram = 0x41414141;

    while(1) {
        ;
    }
}

void dram_read_violation_task()
{
    printf("Reading from forbidden DRAM region\n");

    uint32_t *forbidden_dram = (&_user_data_start) - 0x10000;

    printf("Value : 0x%x\n", *forbidden_dram);

    while(1) {
        ;
    }
}

void rom_reserve_write_violation_task()
{
    printf("Writing to reserve ROM DRAM region\n");

    const ets_rom_layout_t *layout = ets_rom_layout_p;
    *(uint32_t *)layout->dram0_rtos_reserved_start = 0x41414141;

    // Unreachable code as the task will be deleted after
    // accessing reserved DRAM region.
    while(1) {
        ;
    }
}

void rom_reserve_read_violation_task()
{
    printf("Reading from reserve ROM DRAM region\n");

    const ets_rom_layout_t *layout = ets_rom_layout_p;

    printf("Value : 0x%x\n", *(uint32_t *)layout->dram0_rtos_reserved_start);

    // Unreachable code as the task will be deleted after
    // accessing reserved DRAM region.
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

    void (*forbidden_flash_cache)(void) = (void(*)(void))(start - 0x400000);

    forbidden_flash_cache();

    while(1) {
        ;
    }
}

void flash_dcache_violation_task()
{
    char *forbidden_str = (char *)0x3C080130;

    printf("Reading from forbidden Dcache region : %s\n", forbidden_str);

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

    if (xTaskCreate(irom_violation_task, "irom_task", 2048, NULL, 15, NULL) != pdPASS) {
        printf("Task Creation failed\n");
    }

    if (xTaskCreate(sram_icache_write_violation_task, "icache_access_task", 2048, NULL, 14, NULL) != pdPASS) {
        printf("Task Creation failed\n");
    }

    if (xTaskCreate(sram_icache_execute_violation_task, "icache_execute_task", 2048, NULL, 13, NULL) != pdPASS) {
        printf("Task Creation failed\n");
    }

    if (xTaskCreate(iram_execute_violation_task, "iram_execute_task", 2048, NULL, 12, NULL) != pdPASS) {
        printf("Task Creation failed\n");
    }

    if (xTaskCreate(iram_write_violation_task, "iram_write_task", 2048, NULL, 11, NULL) != pdPASS) {
        printf("Task Creation failed\n");
    }

    if (xTaskCreate(iram_read_violation_task, "iram_read_task", 2048, NULL, 10, NULL) != pdPASS) {
        printf("Task Creation failed\n");
    }

    if (xTaskCreate(drom_violation_task, "drom_task", 2048, NULL, 9, NULL) != pdPASS) {
        printf("Task Creation failed\n");
    }

    if (xTaskCreate(dram_write_violation_task, "dram_write_task", 2048, NULL, 8, NULL) != pdPASS) {
        printf("Task Creation failed\n");
    }

    if (xTaskCreate(dram_read_violation_task, "dram_read_task", 2048, NULL, 7, NULL) != pdPASS) {
        printf("Task Creation failed\n");
    }

    if (xTaskCreate(rom_reserve_write_violation_task, "rom_reserve_write_task", 2048, NULL, 6, NULL) != pdPASS) {
        printf("Task Creation failed\n");
    }

    if (xTaskCreate(rom_reserve_read_violation_task, "rom_reserve_read_task", 2048, NULL, 5, NULL) != pdPASS) {
        printf("Task Creation failed\n");
    }

    if (xTaskCreate(rtc_violation_task, "rtc_task", 2048, NULL, 5, NULL) != pdPASS) {
        printf("Task Creation failed\n");
    }

    if (xTaskCreate(pif_violation_task, "pif_task", 2048, NULL, 5, NULL) != pdPASS) {
        printf("Task Creation failed\n");
    }

    if (xTaskCreate(flash_icache_violation_task, "flash_icache_task", 2048, NULL, 5, NULL) != pdPASS) {
        printf("Task Creation failed\n");
    }

    if (xTaskCreate(flash_dcache_violation_task, "flash_dcache_task", 2048, NULL, 5, NULL) != pdPASS) {
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
