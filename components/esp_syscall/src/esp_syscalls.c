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

#include "soc_defs.h"

#include "esp_rom_md5.h"

#ifdef CONFIG_IDF_TARGET_ESP32C3
#include "esp32c3/rom/ets_sys.h"
#endif

#if CONFIG_IDF_TARGET_ARCH_RISCV
#include "riscv/interrupt.h"
#include "riscv/rvruntime-frames.h"
#include "ecall_context.h"
#endif

#define TAG     __func__

typedef void (*syscall_t)(void);

static DRAM_ATTR QueueHandle_t usr_gpio_isr_queue;
static DRAM_ATTR QueueHandle_t usr_event_loop_queue;
static DRAM_ATTR QueueHandle_t usr_xtimer_handler_queue;

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

static int sys_write_log_line(const char *str, size_t len)
{
    if (!is_valid_user_d_addr((void *)str) ||
        !is_valid_user_d_addr((void *)str + len)) {
        return -1;
    }
    return fwrite(str, len, 1, __getreent()->_stdout);
}

static int sys_write_log_line_unlocked(const char *str, size_t len)
{
    if (!is_valid_user_d_addr((void *)str) ||
        !is_valid_user_d_addr((void *)str + len)) {
        return -1;
    }
    if (*(str + len) != '\0') {
        return -1;
    }
    return ets_printf("%s", str);
}

static uint32_t sys_ets_get_cpu_frequency(void)
{
    return ets_get_cpu_frequency();
}

static void sys_esp_rom_md5_init(md5_context_t *context)
{
    if (!is_valid_user_d_addr((void *)context)) {
        return;
    }
    esp_rom_md5_init(context);
}

static void sys_esp_rom_md5_update(md5_context_t *context, const void *buf, uint32_t len)
{
    if (!is_valid_user_d_addr((void *)context) ||
        !is_valid_user_d_addr((void *)buf)) {
        return;
    }
    esp_rom_md5_update(context, buf, len);
}

static void sys_esp_rom_md5_final(uint8_t *digest, md5_context_t *context)
{
    if (!is_valid_user_d_addr((void *)digest) ||
        !is_valid_user_d_addr((void *)context)) {
        return;
    }
    esp_rom_md5_final(digest, context);
}

static int is_valid_user_task(TaskHandle_t xTask)
{
    if (pvTaskGetThreadLocalStoragePointer(xTask, 1) == NULL) {
        /* This means this task is a kernel task.
         * User task has a kernel stack at this index
         */
        return 0;
    } else {
        return 1;
    }
}

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
    memset(kernel_stack, tskSTACK_FILL_BYTE, KERNEL_STACK_SIZE * sizeof(StackType_t));

    /* Suspend the scheduler to ensure that it does not switch to the newly created task.
     * We need to first set the kernel stack as a TLS and only then it should be executed
     */
    vTaskSuspendAll();
    handle = xTaskCreateStatic(pvTaskCode, pcName, usStackDepth, pvParameters, uxPriority, xtaskStack, xtaskTCB);

    // The 1st (0th index) TLS pointer is used by WiFi
    // 2nd TLS pointer is used to store the kernel stack, used when servicing system calls
    // 3rd TLS pointer is used to store the WORLD of the task. 0 = WORLD0 and 1 = WORLD1
    vTaskSetThreadLocalStoragePointerAndDelCallback(handle, 1, kernel_stack, NULL);
    vTaskSetThreadLocalStoragePointerAndDelCallback(handle, 2, (void *)1, NULL);
    xTaskResumeAll();

    if (pvCreatedTask) {
        *pvCreatedTask = handle;
    }
    return pdPASS;
}

void vPortCleanUpTCB (void *pxTCB)
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

static void sys_vTaskDelay(TickType_t TickstoWait)
{
    vTaskDelay(TickstoWait);
}

static void sys_vTaskDelayUntil(TickType_t * const pxPreviousWakeTime, const TickType_t xTimeIncrement)
{
    vTaskDelayUntil(pxPreviousWakeTime, xTimeIncrement);
}

static UBaseType_t sys_uxTaskPriorityGet(const TaskHandle_t xTask)
{
    return uxTaskPriorityGet(xTask);
}

static void sys_vTaskPrioritySet(TaskHandle_t xTask, UBaseType_t uxNewPriority)
{
    if (!is_valid_user_task(xTask)) {
        ESP_LOGE(TAG, "Invalid task handle");
        return;
    }

    vTaskPrioritySet(xTask, uxNewPriority);
}

static UBaseType_t sys_uxTaskPriorityGetFromISR(const TaskHandle_t xTask)
{
    return uxTaskPriorityGetFromISR(xTask);
}

static void sys_vTaskSuspend(TaskHandle_t xTaskToSuspend)
{
    if (!is_valid_user_task(xTaskToSuspend)) {
        ESP_LOGE(TAG, "Invalid task handle");
        return;
    }

    vTaskSuspend(xTaskToSuspend);
}

static void sys_vTaskResume(TaskHandle_t xTaskToResume)
{
    if (!is_valid_user_task(xTaskToResume)) {
        ESP_LOGE(TAG, "Invalid task handle");
        return;
    }

    vTaskResume(xTaskToResume);
}

static BaseType_t sys_xTaskResumeFromISR(TaskHandle_t xTaskToResume)
{
    if (!is_valid_user_task(xTaskToResume)) {
        ESP_LOGE(TAG, "Invalid task handle");
        return 0;
    }

    return xTaskResumeFromISR(xTaskToResume);
}

static BaseType_t sys_xTaskAbortDelay(TaskHandle_t xTask)
{
#if INCLUDE_xTaskAbortDelay
    if (!is_valid_user_task(xTask)) {
        ESP_LOGE(TAG, "Invalid task handle");
        return 0;
    }

    return xTaskAbortDelay(xTask);
#endif
    return 0;
}

static void sys_vPortYield(void)
{
    vPortYield();
}

static void sys_vPortEnterCritical(void)
{
    vPortEnterCritical();
}

static void sys_vPortExitCritical(void)
{
    vPortExitCritical();
}

static int sys_vPortSetInterruptMask(void)
{
    return vPortSetInterruptMask();
}

static void sys_vPortClearInterruptMask(int mask)
{
    vPortClearInterruptMask(mask);
}

static void sys_vTaskSuspendAll(void)
{
    vTaskSuspendAll();
}

static BaseType_t sys_xTaskResumeAll(void)
{
    return xTaskResumeAll();
}

static void sys_vTaskStepTick(const TickType_t xTicksToJump)
{
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
    vTaskStepTick(xTicksToJump);
#endif
}

static BaseType_t sys_xTaskGenericNotify(TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction, uint32_t *pulPreviousNotificationValue)
{
    if (!is_valid_user_task(xTaskToNotify)) {
        ESP_LOGE(TAG, "Invalid task handle");
        return 0;
    }

    return xTaskGenericNotify(xTaskToNotify, ulValue, eAction, pulPreviousNotificationValue);
}

static BaseType_t sys_xTaskGenericNotifyFromISR(TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction, uint32_t *pulPreviousNotificationValue, BaseType_t *pxHigherPriorityTaskWoken)
{
    if (!is_valid_user_task(xTaskToNotify)) {
        ESP_LOGE(TAG, "Invalid task handle");
        return 0;
    }

    return xTaskGenericNotifyFromISR(xTaskToNotify, ulValue, eAction, pulPreviousNotificationValue, pxHigherPriorityTaskWoken);
}

static void sys_vTaskNotifyGiveFromISR(TaskHandle_t xTaskToNotify, BaseType_t *pxHigherPriorityTaskWoken)
{
    if (!is_valid_user_task(xTaskToNotify)) {
        ESP_LOGE(TAG, "Invalid task handle");
        return;
    }

    vTaskNotifyGiveFromISR(xTaskToNotify, pxHigherPriorityTaskWoken);
}

static uint32_t sys_ulTaskNotifyTake(BaseType_t xClearCountOnExit, TickType_t xTicksToWait)
{
    return ulTaskNotifyTake(xClearCountOnExit, xTicksToWait);
}

static BaseType_t sys_xTaskNotifyWait(uint32_t ulBitsToClearOnEntry, uint32_t ulBitsToClearOnExit, uint32_t *pulNotificationValue, TickType_t xTicksToWait)
{
    return xTaskNotifyWait(ulBitsToClearOnEntry, ulBitsToClearOnExit, pulNotificationValue, xTicksToWait);
}

static BaseType_t sys_xTaskNotifyStateClear(TaskHandle_t xTask)
{
    if (!is_valid_user_task(xTask)) {
        ESP_LOGE(TAG, "Invalid task handle");
        return 0;
    }

    return xTaskNotifyStateClear(xTask);
}

static TaskHandle_t sys_xTaskGetCurrentTaskHandle(void)
{
    return xTaskGetCurrentTaskHandle();
}

static TaskHandle_t sys_xTaskGetIdleTaskHandle(void)
{
    return xTaskGetIdleTaskHandle();
}

static UBaseType_t sys_uxTaskGetStackHighWaterMark(TaskHandle_t xTask)
{
    if (!is_valid_user_task(xTask)) {
        ESP_LOGE(TAG, "Invalid task handle");
        return 0;
    }

    return uxTaskGetStackHighWaterMark(xTask);
}

static eTaskState sys_eTaskGetState(TaskHandle_t xTask)
{
    if (!is_valid_user_task(xTask)) {
        ESP_LOGE(TAG, "Invalid task handle");
        return 0;
    }

    return eTaskGetState(xTask);
}

static char *sys_pcTaskGetName(TaskHandle_t xTaskToQuery)
{
    if (!is_valid_user_task(xTaskToQuery)) {
        ESP_LOGE(TAG, "Invalid task handle");
        return NULL;
    }

    return pcTaskGetName(xTaskToQuery);
}

static TaskHandle_t sys_xTaskGetHandle(const char *pcNameToQuery)
{
#if INCLUDE_xTaskGetHandle
    if (is_valid_user_d_addr((void *)pcNameToQuery)) {
        return xTaskGetHandle(pcNameToQuery);
    } else {
        return NULL;
    }
#endif
    return 0;
}

static TickType_t sys_xTaskGetTickCount(void)
{
    return xTaskGetTickCount();
}

static TickType_t sys_xTaskGetTickCountFromISR(void)
{
    return xTaskGetTickCountFromISR();
}

static BaseType_t sys_xTaskGetSchedulerState(void)
{
    return xTaskGetSchedulerState();
}

static UBaseType_t sys_uxTaskGetNumberOfTasks(void)
{
    return uxTaskGetNumberOfTasks();
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

static void sys_vQueueDelete(QueueHandle_t xQueue)
{
    if (is_valid_kernel_d_addr(xQueue)) {
        vQueueDelete(xQueue);
        free(((StaticQueue_t *)xQueue)->pvDummy1[0]);
        free(xQueue);
    }
}

static BaseType_t sys_xQueueGenericSend(QueueHandle_t xQueue, const void * const pvItemToQueue, TickType_t xTicksToWait, const BaseType_t xCopyPosition)
{
    // Incase of Semaphore, pvItemToQueue is NULL. Add an extra check for uxItemSize, which for Semaphore is 0
    // If the uxItemSize if 0, we can allow invalid pvItemToQueue pointer
    if (is_valid_kernel_d_addr(xQueue) &&
        (is_valid_user_d_addr((void *)pvItemToQueue) || ((StaticQueue_t *)xQueue)->uxDummy4[2] == 0)) {
        return xQueueGenericSend(xQueue, pvItemToQueue, xTicksToWait, xCopyPosition);
    } else {
        return 0;
    }
}

static BaseType_t sys_xQueueGenericSendFromISR(QueueHandle_t xQueue, const void * const pvItemToQueue, BaseType_t * const pxHigherPriorityTaskWoken, const BaseType_t xCopyPosition)
{
    if (is_valid_kernel_d_addr(xQueue) && is_valid_user_d_addr((void *)pvItemToQueue)) {
        return xQueueGenericSendFromISR(xQueue, pvItemToQueue, pxHigherPriorityTaskWoken, xCopyPosition);
    } else {
        return 0;
    }
}

static int sys_xQueueReceive(QueueHandle_t xQueue, void * const buffer, TickType_t TickstoWait)
{
    if (is_valid_kernel_d_addr(xQueue) && is_valid_user_d_addr(buffer)) {
        return xQueueReceive(xQueue, buffer, TickstoWait);
    }
    return -1;
}

static BaseType_t sys_xQueueReceiveFromISR(QueueHandle_t xQueue, void * const pvBuffer, BaseType_t * const pxHigherPriorityTaskWoken)
{
    if (is_valid_kernel_d_addr(xQueue) && is_valid_user_d_addr((void *)pvBuffer)) {
        return xQueueReceiveFromISR(xQueue, pvBuffer, pxHigherPriorityTaskWoken);
    } else {
        return 0;
    }
}

static UBaseType_t sys_uxQueueMessagesWaiting(const QueueHandle_t xQueue)
{
    return uxQueueMessagesWaiting(xQueue);
}

static UBaseType_t sys_uxQueueMessagesWaitingFromISR(const QueueHandle_t xQueue)
{
    return uxQueueMessagesWaitingFromISR(xQueue);
}

static UBaseType_t sys_uxQueueSpacesAvailable(const QueueHandle_t xQueue)
{
    return uxQueueSpacesAvailable(xQueue);
}

static BaseType_t sys_xQueueGenericReset(QueueHandle_t xQueue, BaseType_t xNewQueue)
{
    return xQueueGenericReset(xQueue, xNewQueue);
}

static BaseType_t sys_xQueuePeek(QueueHandle_t xQueue, void * const pvBuffer, TickType_t xTicksToWait)
{
    if (is_valid_kernel_d_addr(xQueue) && is_valid_user_d_addr((void *)pvBuffer)) {
        return xQueuePeek(xQueue, pvBuffer, xTicksToWait);
    } else {
        return 0;
    }
}

static BaseType_t sys_xQueuePeekFromISR(QueueHandle_t xQueue,  void * const pvBuffer)
{
    if (is_valid_kernel_d_addr(xQueue) && is_valid_user_d_addr((void *)pvBuffer)) {
        return xQueuePeekFromISR(xQueue, pvBuffer);
    } else {
        return 0;
    }
}

static BaseType_t sys_xQueueIsQueueEmptyFromISR(const QueueHandle_t xQueue)
{
    return xQueueIsQueueEmptyFromISR(xQueue);
}

static BaseType_t sys_xQueueIsQueueFullFromISR(const QueueHandle_t xQueue)
{
    return xQueueIsQueueFullFromISR(xQueue);
}

static QueueSetHandle_t sys_xQueueCreateSet(const UBaseType_t uxEventQueueLength)
{
    QueueSetHandle_t pxQueue;

    pxQueue = sys_xQueueGenericCreate(uxEventQueueLength, sizeof(QueueHandle_t *), queueQUEUE_TYPE_SET);

    return pxQueue;
}

static BaseType_t sys_xQueueAddToSet(QueueSetMemberHandle_t xQueueOrSemaphore, QueueSetHandle_t xQueueSet)
{
    return xQueueAddToSet(xQueueOrSemaphore, xQueueSet);
}

static BaseType_t sys_xQueueRemoveFromSet(QueueSetMemberHandle_t xQueueOrSemaphore, QueueSetHandle_t xQueueSet)
{
    return xQueueRemoveFromSet(xQueueOrSemaphore, xQueueSet);
}

static QueueSetMemberHandle_t sys_xQueueSelectFromSet(QueueSetHandle_t xQueueSet, TickType_t const xTicksToWait)
{
    return xQueueSelectFromSet(xQueueSet, xTicksToWait);
}

static QueueSetMemberHandle_t sys_xQueueSelectFromSetFromISR(QueueSetHandle_t xQueueSet)
{
    return xQueueSelectFromSetFromISR(xQueueSet);
}

static QueueHandle_t sys_xQueueCreateCountingSemaphore(const UBaseType_t uxMaxCount, const UBaseType_t uxInitialCount)
{
    return xQueueCreateCountingSemaphore(uxMaxCount, uxInitialCount);
}

static QueueHandle_t sys_xQueueCreateMutex(const uint8_t ucQueueType)
{
    return xQueueCreateMutex(ucQueueType);
}

static TaskHandle_t sys_xQueueGetMutexHolder(QueueHandle_t xSemaphore)
{
    return xQueueGetMutexHolder(xSemaphore);
}

static BaseType_t sys_xQueueSemaphoreTake(QueueHandle_t xQueue, TickType_t xTicksToWait)
{
    return xQueueSemaphoreTake(xQueue, xTicksToWait);
}

static BaseType_t sys_xQueueTakeMutexRecursive(QueueHandle_t xMutex, TickType_t xTicksToWait)
{
    return xQueueTakeMutexRecursive(xMutex, xTicksToWait);
}

static BaseType_t sys_xQueueGiveMutexRecursive(QueueHandle_t xMutex)
{
    return xQueueGiveMutexRecursive(xMutex);
}

static BaseType_t sys_xQueueGiveFromISR(QueueHandle_t xQueue, BaseType_t * const pxHigherPriorityTaskWoken)
{
    return xQueueGiveFromISR(xQueue, pxHigherPriorityTaskWoken);
}

static void sys_xtimer_cb(void *timer)
{
    usr_xtimer_context_t* usr_context = (usr_xtimer_context_t*)pvTimerGetTimerID(timer);

    if (xQueueSend(usr_xtimer_handler_queue, usr_context, portMAX_DELAY) != pdPASS) {
        return;
    }
}

static TimerHandle_t sys_xTimerCreate(const char * const pcTimerName,
                                      const TickType_t xTimerPeriodInTicks,
                                      const UBaseType_t uxAutoReload,
                                      void * const pvTimerID,
                                      TimerCallbackFunction_t pxCallbackFunction,
                                      QueueHandle_t usr_queue)
{
    TimerHandle_t handle;

    if (!(is_valid_user_d_addr(pxCallbackFunction) && usr_queue)) {
        ESP_LOGE(TAG, "Incorrect address space for callback function or user queue");
        return NULL;
    }

    usr_xtimer_handler_queue = usr_queue;

    usr_xtimer_context_t *usr_context = heap_caps_malloc(sizeof(usr_xtimer_context_t), MALLOC_CAP_WORLD1);
    if (!usr_context) {
        ESP_LOGE(TAG, "Insufficient memory for user context");
        return NULL;
    }

    handle = xTimerCreate(pcTimerName, xTimerPeriodInTicks, uxAutoReload, usr_context, sys_xtimer_cb);

    if (handle == NULL) {
        free(usr_context);
    } else {
        usr_context->usr_cb = pxCallbackFunction;
        usr_context->usr_timer_id = pvTimerID;
        usr_context->timerhandle = handle;
    }
    return handle;
}

static BaseType_t sys_xTimerIsTimerActive(TimerHandle_t xTimer)
{
    return xTimerIsTimerActive(xTimer);
}

static void *sys_pvTimerGetTimerID(const TimerHandle_t xTimer)
{
    usr_xtimer_context_t *usr_context = (usr_xtimer_context_t*)pvTimerGetTimerID(xTimer);

    return usr_context->usr_timer_id;
}

static const char *sys_pcTimerGetName(TimerHandle_t xTimer)
{
    return pcTimerGetName(xTimer);
}

static void sys_vTimerSetReloadMode(TimerHandle_t xTimer, const UBaseType_t uxAutoReload)
{
    vTimerSetReloadMode(xTimer, uxAutoReload);
}

static BaseType_t sys_xTimerGenericCommand(TimerHandle_t xTimer, const BaseType_t xCommandID, const TickType_t xOptionalValue, BaseType_t * const pxHigherPriorityTaskWoken, const TickType_t xTicksToWait)
{
    return xTimerGenericCommand(xTimer, xCommandID, xOptionalValue, pxHigherPriorityTaskWoken, xTicksToWait);
}

static void sys_vTimerSetTimerID(TimerHandle_t xTimer, void *pvNewID)
{
    usr_xtimer_context_t *usr_context = (usr_xtimer_context_t*)pvTimerGetTimerID(xTimer);

    usr_context->usr_timer_id = pvNewID;
}

static TickType_t sys_xTimerGetPeriod(TimerHandle_t xTimer)
{
    return xTimerGetPeriod(xTimer);
}

static TickType_t sys_xTimerGetExpiryTime(TimerHandle_t xTimer)
{
    return xTimerGetExpiryTime(xTimer);
}

static EventGroupHandle_t sys_xEventGroupCreate(void)
{
    return xEventGroupCreate();
}

static EventBits_t sys_xEventGroupWaitBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToWaitFor, const BaseType_t xClearOnExit, const BaseType_t xWaitForAllBits, TickType_t xTicksToWait)
{
    return xEventGroupWaitBits(xEventGroup, uxBitsToWaitFor, xClearOnExit, xWaitForAllBits, xTicksToWait);
}

static EventBits_t sys_xEventGroupSetBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet)
{
    return xEventGroupSetBits(xEventGroup, uxBitsToSet);
}

static EventBits_t sys_xEventGroupClearBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToClear)
{
    return xEventGroupClearBits(xEventGroup, uxBitsToClear);
}

static EventBits_t sys_xEventGroupSync(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet, const EventBits_t uxBitsToWaitFor, TickType_t xTicksToWait)
{
    return xEventGroupSync(xEventGroup, uxBitsToSet, uxBitsToWaitFor, xTicksToWait);
}

static void sys_vEventGroupDelete(EventGroupHandle_t xEventGroup)
{
    vEventGroupDelete(xEventGroup);
    return;
}

static int sys_lwip_socket(int domain, int type, int protocol)
{
    return lwip_socket(domain, type, protocol);
}

static int sys_lwip_connect(int s, const struct sockaddr *name, socklen_t namelen)
{
    return lwip_connect(s, name, namelen);
}

static int sys_lwip_accept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
    if (!is_valid_user_d_addr(addr) || !is_valid_user_d_addr(addrlen)) {
        return -1;
    }
    return lwip_accept(s, addr, addrlen);
}

static int sys_lwip_bind(int s, const struct sockaddr *name, socklen_t namelen)
{
    if (!is_valid_user_d_addr((void *)name)) {
        return -1;
    }
    return lwip_bind(s, name, namelen);
}

static int sys_lwip_shutdown(int s, int how)
{
    return lwip_shutdown(s, how);
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

static int sys_lwip_getpeername(int s, struct sockaddr *name, socklen_t *namelen)
{
    if (!is_valid_user_d_addr((void *)name) || !is_valid_user_d_addr((void *)namelen)) {
        return -1;
    }

    return lwip_getpeername(s, name, namelen);
}

static int sys_lwip_getsockname(int s, struct sockaddr *name, socklen_t *namelen)
{
    if (!is_valid_user_d_addr((void *)name) || !is_valid_user_d_addr((void *)namelen)) {
        return -1;
    }

    return lwip_getsockname(s, name, namelen);
}

static int sys_lwip_setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen)
{
    return lwip_setsockopt(s, level, optname, optval, optlen);
}

static int sys_lwip_getsockopt(int s, int level, int optname, void *optval, socklen_t *optlen)
{
    return lwip_getsockopt(s, level, optname, optval, optlen);
}

static int sys_lwip_listen(int s, int backlog)
{
    return lwip_listen(s, backlog);
}

static size_t sys_lwip_recv(int s, void *mem, size_t len, int flags)
{
    return lwip_recv(s, mem, len, flags);
}

static ssize_t sys_lwip_recvmsg(int s, struct msghdr *message, int flags)
{
    if (!is_valid_user_d_addr((void *)message) ||
        !is_valid_user_d_addr((void *)message->msg_iov) ||
        !is_valid_user_d_addr((void *)message->msg_iov->iov_base) ||
        !is_valid_user_d_addr((void *)message->msg_control)) {
        return -1;
    }

    return lwip_recvmsg(s, message, flags);
}

static ssize_t sys_lwip_recvfrom(int s, void *mem, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen)
{
    if (!is_valid_user_d_addr((void *)mem) ||
        !is_valid_user_d_addr((void *)from) ||
        !is_valid_user_d_addr((void *)fromlen)) {
        return -1;
    }

    return lwip_recvfrom(s, mem, len, flags, from, fromlen);
}

static size_t sys_lwip_send(int s, const void *data, size_t size, int flags)
{
    return lwip_send(s, data, size, flags);
}

static ssize_t sys_lwip_sendmsg(int s, const struct msghdr *msg, int flags)
{
    if (!is_valid_user_d_addr((void *)msg) ||
        !is_valid_user_d_addr((void *)msg->msg_iov) ||
        !is_valid_user_d_addr((void *)msg->msg_iov->iov_base) ||
        !is_valid_user_d_addr((void *)msg->msg_control)) {
        return -1;
    }

    return lwip_sendmsg(s, msg, flags);
}

static ssize_t sys_lwip_sendto(int s, const void *data, size_t size, int flags, const struct sockaddr *to, socklen_t tolen)
{
    if (!is_valid_user_d_addr((void *)data) ||
        !is_valid_user_d_addr((void *)to)) {
        return -1;
    }

    return lwip_sendto(s, data, size, flags, to, tolen);
}

static const char *sys_lwip_inet_ntop(int af, const void *src, char *dst, socklen_t size)
{
    if (!is_valid_user_d_addr((void *)src) ||
        !is_valid_user_d_addr((void *)dst)) {
        return NULL;
    }

    return lwip_inet_ntop(af, src, dst, size);
}

static int sys_lwip_inet_pton(int af, const char *src, void *dst)
{
    if (!is_valid_user_d_addr((void *)src) ||
        !is_valid_user_d_addr((void *)dst)) {
        return -1;
    }

    return lwip_inet_pton(af, src, dst);
}

static u32_t sys_lwip_htonl(u32_t n)
{
    return lwip_htonl(n);
}

static u16_t sys_lwip_htons(u16_t n)
{
    return lwip_htons(n);
}

static int *sys___errno(void)
{
    return __errno();
}

static int sys_open(const char *path, int flags, int mode)
{
    if (is_valid_user_d_addr((void *)path)) {
        return open(path, flags, mode);
    }
    return -1;
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

static int sys_select(int maxfdp1, fd_set *readset, fd_set *writeset, fd_set *exceptset, struct timeval *timeout)
{
    if (is_valid_kernel_d_addr((void *)readset) ||
        is_valid_kernel_d_addr((void *)writeset) ||
        is_valid_kernel_d_addr((void *)exceptset)) {
        return -1;
    }

    return select(maxfdp1, readset, writeset, exceptset, timeout);
}

static int sys_poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    if (!is_valid_user_d_addr((void *)fds)) {
        return -1;
    }

    return poll(fds, nfds, timeout);
}

static int sys_ioctl(int s, long cmd, void *argp)
{
    if (!is_valid_user_d_addr((void *)argp)) {
        return -1;
    }

    return ioctl(s, cmd, argp);
}

static int sys_fcntl(int s, int cmd, int val)
{
    return fcntl(s, cmd, val);
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

static void *sys_malloc(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_WORLD1);
}

static void *sys_calloc(size_t nmemb, size_t size)
{
    return heap_caps_calloc(nmemb, size, MALLOC_CAP_WORLD1);
}

static void sys_free(void *ptr)
{
    free(ptr);
}

static void *sys_realloc(void* ptr, size_t size)
{
    return heap_caps_realloc(ptr, size, MALLOC_CAP_WORLD1);
}

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
#endif
const syscall_t syscall_table[__NR_syscalls] = {
    [0 ... __NR_syscalls - 1] = (syscall_t)&sys_ni_syscall,

#define __SYSCALL(nr, symbol, nargs)  [nr] = (syscall_t)symbol,
#include "esp_syscall.h"
};
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
