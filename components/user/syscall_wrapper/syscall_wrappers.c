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
#include "esp_idf_version.h"
#include "syscall_def.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "syscall_wrappers.h"
#include "syscall_structs.h"
#include "esp_event.h"
#include "esp_wifi.h"

#include <lwip/sockets.h>
#include <lwip/netdb.h>

#include "esp_heap_caps_init.h"
#include "esp_rom_md5.h"
#include "esp_log.h"

#ifdef CONFIG_IDF_TARGET_ESP32C3
#include "esp32c3/rom/ets_sys.h"
#include "esp32c3/rom/rom_layout.h"
#endif

#include <driver/uart.h>

#ifndef XTSTR
#define _XTSTR(x)	# x
#define XTSTR(x)	_XTSTR(x)
#endif

static bool _is_heap_initialized;

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wint-conversion"
#endif

/* Reserve 256 bytes for user app descriptor required by esptool while generating user app binary.
 * esptool inserts SHA256 at 144th offset, so it needs to be all zeros
 *
 * Not required to explicitly reserve because app_update component is now included in the user_app
 */
//const __attribute__((section(".rodata_desc"))) uint8_t user_app_desc[256];

// ROM and Newlib
int usr_putchar(int c)
{
    return EXECUTE_SYSCALL(c, __NR_putchar);
}

int usr_write_log_line(const char *str, size_t len)
{
    return EXECUTE_SYSCALL(str, len, __NR_write_log_line);
}

int usr_write_log_line_unlocked(const char *str, size_t len)
{
    return EXECUTE_SYSCALL(str, len, __NR_write_log_line_unlocked);
}

uint32_t usr_ets_get_cpu_frequency(void)
{
    return EXECUTE_SYSCALL(__NR_ets_get_cpu_frequency);
}

void usr_esp_rom_md5_init(md5_context_t *context)
{
    EXECUTE_SYSCALL(context, __NR_esp_rom_md5_init);
}

void usr_esp_rom_md5_update(md5_context_t *context, const void *buf, uint32_t len)
{
    EXECUTE_SYSCALL(context, buf, len, __NR_esp_rom_md5_update);
}

void usr_esp_rom_md5_final(uint8_t *digest, md5_context_t *context)
{
    EXECUTE_SYSCALL(digest, context, __NR_esp_rom_md5_final);
}

int usr_esp_rom_uart_tx_one_char(uint8_t c)
{
    return EXECUTE_SYSCALL(c, __NR_esp_rom_uart_tx_one_char);
}

int usr_esp_rom_uart_rx_one_char(uint8_t *c)
{
    return EXECUTE_SYSCALL(c, __NR_esp_rom_uart_rx_one_char);
}

UIRAM_ATTR void usr__lock_acquire(_lock_t *lock)
{
    EXECUTE_SYSCALL(lock, __NR__lock_acquire);
}

UIRAM_ATTR void usr__lock_acquire_recursive(_lock_t *lock)
{
    EXECUTE_SYSCALL(lock, __NR__lock_acquire_recursive);
}

UIRAM_ATTR int usr__lock_try_acquire(_lock_t *lock)
{
    return EXECUTE_SYSCALL(lock, __NR__lock_try_acquire);
}

UIRAM_ATTR int usr__lock_try_acquire_recursive(_lock_t *lock)
{
    return EXECUTE_SYSCALL(lock, __NR__lock_try_acquire_recursive);
}

UIRAM_ATTR void usr__lock_release(_lock_t *lock)
{
    EXECUTE_SYSCALL(lock, __NR__lock_release);
}

IRAM_ATTR void usr__lock_release_recursive(_lock_t *lock)
{
    EXECUTE_SYSCALL(lock, __NR__lock_release_recursive);
}

UIRAM_ATTR void usr___retarget_lock_acquire(_LOCK_T lock)
{
    EXECUTE_SYSCALL(lock, __NR___retarget_lock_acquire);
}

UIRAM_ATTR void usr___retarget_lock_acquire_recursive(_LOCK_T lock)
{
    EXECUTE_SYSCALL(lock, __NR___retarget_lock_acquire_recursive);
}

UIRAM_ATTR int usr___retarget_lock_try_acquire(_LOCK_T lock)
{
    return EXECUTE_SYSCALL(lock, __NR___retarget_lock_try_acquire);
}

UIRAM_ATTR int usr___retarget_lock_try_acquire_recursive(_LOCK_T lock)
{
    return EXECUTE_SYSCALL(lock, __NR___retarget_lock_try_acquire_recursive);
}

UIRAM_ATTR void usr___retarget_lock_release(_LOCK_T lock)
{
    EXECUTE_SYSCALL(lock, __NR___retarget_lock_release);
}

IRAM_ATTR void usr___retarget_lock_release_recursive(_LOCK_T lock)
{
    EXECUTE_SYSCALL(lock, __NR___retarget_lock_release_recursive);
}

void usr_esp_time_impl_set_boot_time(uint64_t time_us)
{
    EXECUTE_SYSCALL(time_us, __NR_esp_time_impl_set_boot_time);
}

uint64_t usr_esp_time_impl_get_boot_time(void)
{
    return EXECUTE_SYSCALL(__NR_esp_time_impl_get_boot_time);
}

// Task Creation
BaseType_t usr_xTaskCreatePinnedToCore(TaskFunction_t pvTaskCode,
                                   const char * const pcName,
                                   const uint32_t usStackDepth,
                                   void * const pvParameters,
                                   UBaseType_t uxPriority,
                                   TaskHandle_t * const pvCreatedTask,
                                   const BaseType_t xCoreID)
{
    int ret;
    usr_task_ctx_t task_ctx;
    task_ctx.stack = heap_caps_malloc(usStackDepth, MALLOC_CAP_DEFAULT);
    if (task_ctx.stack == NULL) {
        return -1;
    }

    task_ctx.task_errno = heap_caps_malloc(sizeof(int), MALLOC_CAP_DEFAULT);
    if (task_ctx.task_errno == NULL) {
        free(task_ctx.stack);
        return -1;
    }
    task_ctx.task_handle = pvCreatedTask;
    task_ctx.stack_size = usStackDepth;

    // We don't support more than 6 arguments for a system call, so to pass more arguments, we use usr_task_ctx_t struct
    ret = EXECUTE_SYSCALL(pvTaskCode, pcName, pvParameters, uxPriority, xCoreID, &task_ctx, __NR_xTaskCreatePinnedToCore);

    if (ret != pdPASS) {
        free(task_ctx.stack);
        free(task_ctx.task_errno);
    }

    return ret;
}

void usr_vTaskDelete(TaskHandle_t TaskHandle)
{
    EXECUTE_SYSCALL(TaskHandle, __NR_vTaskDelete);
}

// Task control
void usr_vTaskDelay(TickType_t TickstoWait)
{
    EXECUTE_SYSCALL(TickstoWait, __NR_vTaskDelay);
}

void usr_vTaskDelayUntil(TickType_t * const pxPreviousWakeTime, const TickType_t xTimeIncrement)
{
    EXECUTE_SYSCALL(pxPreviousWakeTime, xTimeIncrement, __NR_vTaskDelayUntil);
}

UBaseType_t usr_uxTaskPriorityGet(const TaskHandle_t xTask)
{
    return EXECUTE_SYSCALL(xTask, __NR_uxTaskPriorityGet);
}

void usr_vTaskPrioritySet(TaskHandle_t xTask, UBaseType_t uxNewPriority)
{
    EXECUTE_SYSCALL(xTask, uxNewPriority, __NR_vTaskPrioritySet);
}

UBaseType_t usr_uxTaskPriorityGetFromISR(const TaskHandle_t xTask)
{
    return EXECUTE_SYSCALL(xTask, __NR_uxTaskPriorityGetFromISR);
}

void usr_vTaskSuspend(TaskHandle_t xTaskToSuspend)
{
    EXECUTE_SYSCALL(xTaskToSuspend, __NR_vTaskSuspend);
}

void usr_vTaskResume(TaskHandle_t xTaskToResume)
{
    EXECUTE_SYSCALL(xTaskToResume, __NR_vTaskResume);
}

BaseType_t usr_xTaskResumeFromISR(TaskHandle_t xTaskToResume)
{
    return EXECUTE_SYSCALL(xTaskToResume, __NR_xTaskResumeFromISR);
}

BaseType_t usr_xTaskAbortDelay(TaskHandle_t xTask)
{
    return EXECUTE_SYSCALL(xTask, __NR_xTaskAbortDelay);
}

// Critical APIs
void usr_vPortYield(void)
{
    EXECUTE_SYSCALL(__NR_vPortYield);
}

void usr_vPortEnterCritical(void)
{
    EXECUTE_SYSCALL(__NR_vPortEnterCritical);
}

void usr_vPortExitCritical(void)
{
    EXECUTE_SYSCALL(__NR_vPortExitCritical);
}

int usr_vPortSetInterruptMask(void)
{
    return EXECUTE_SYSCALL(__NR_vPortSetInterruptMask);
}

void usr_vPortClearInterruptMask(int mask)
{
    EXECUTE_SYSCALL(mask, __NR_vPortClearInterruptMask);
}

void usr_vTaskSuspendAll(void)
{
    EXECUTE_SYSCALL(__NR_vTaskSuspendAll);
}

BaseType_t usr_xTaskResumeAll(void)
{
    return EXECUTE_SYSCALL(__NR_xTaskResumeAll);
}

void usr_vTaskStepTick(const TickType_t xTicksToJump)
{
    EXECUTE_SYSCALL(xTicksToJump, __NR_vTaskStepTick);
}

// Task Notifications
#if defined(IDF_VERSION_V4_3)
BaseType_t usr_xTaskGenericNotify(TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction, uint32_t *pulPreviousNotificationValue)
#elif defined(IDF_VERSION_V4_4)
BaseType_t usr_xTaskGenericNotify(TaskHandle_t xTaskToNotify, UBaseType_t uxIndexToNotify, uint32_t ulValue, eNotifyAction eAction, uint32_t *pulPreviousNotificationValue)
#endif
{
#if defined(IDF_VERSION_V4_3)
    return EXECUTE_SYSCALL(xTaskToNotify, ulValue, eAction, pulPreviousNotificationValue, __NR_xTaskGenericNotify);
#elif defined(IDF_VERSION_V4_4)
    return EXECUTE_SYSCALL(xTaskToNotify, uxIndexToNotify, ulValue, eAction, pulPreviousNotificationValue, __NR_xTaskGenericNotify);
#endif
}

#if defined(IDF_VERSION_V4_3)
BaseType_t usr_xTaskGenericNotifyFromISR(TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction, uint32_t *pulPreviousNotificationValue, BaseType_t *pxHigherPriorityTaskWoken)
#elif defined(IDF_VERSION_V4_4)
BaseType_t usr_xTaskGenericNotifyFromISR(TaskHandle_t xTaskToNotify, UBaseType_t uxIndexToNotify, uint32_t ulValue, eNotifyAction eAction, uint32_t *pulPreviousNotificationValue, BaseType_t *pxHigherPriorityTaskWoken)
#endif
{
#if defined(IDF_VERSION_V4_3)
    return EXECUTE_SYSCALL(xTaskToNotify, ulValue, eAction, pulPreviousNotificationValue, pxHigherPriorityTaskWoken, __NR_xTaskGenericNotifyFromISR);
#elif defined(IDF_VERSION_V4_4)
    return EXECUTE_SYSCALL(xTaskToNotify, uxIndexToNotify, ulValue, eAction, pulPreviousNotificationValue, pxHigherPriorityTaskWoken, __NR_xTaskGenericNotifyFromISR);
#endif
}

void usr_vTaskNotifyGiveFromISR(TaskHandle_t xTaskToNotify, BaseType_t *pxHigherPriorityTaskWoken)
{
    EXECUTE_SYSCALL(xTaskToNotify, pxHigherPriorityTaskWoken, __NR_vTaskNotifyGiveFromISR);
}

uint32_t usr_ulTaskNotifyTake(BaseType_t xClearCountOnExit, TickType_t xTicksToWait)
{
    return EXECUTE_SYSCALL(xClearCountOnExit, xTicksToWait, __NR_ulTaskNotifyTake);
}

BaseType_t usr_xTaskNotifyWait(uint32_t ulBitsToClearOnEntry, uint32_t ulBitsToClearOnExit, uint32_t *pulNotificationValue, TickType_t xTicksToWait)
{
    return EXECUTE_SYSCALL(ulBitsToClearOnEntry, ulBitsToClearOnExit, pulNotificationValue, xTicksToWait, __NR_xTaskNotifyWait);
}

BaseType_t usr_xTaskNotifyStateClear(TaskHandle_t xTask)
{
    return EXECUTE_SYSCALL(xTask, __NR_xTaskNotifyStateClear);
}

// Task Utilities
TaskHandle_t usr_xTaskGetCurrentTaskHandle(void)
{
    return EXECUTE_SYSCALL(__NR_xTaskGetCurrentTaskHandle);
}

TaskHandle_t usr_xTaskGetIdleTaskHandle(void)
{
    return EXECUTE_SYSCALL(__NR_xTaskGetIdleTaskHandle);
}

UBaseType_t usr_uxTaskGetStackHighWaterMark(TaskHandle_t xTask)
{
    return EXECUTE_SYSCALL(xTask, __NR_uxTaskGetStackHighWaterMark);
}

eTaskState usr_eTaskGetState(TaskHandle_t xTask)
{
    return EXECUTE_SYSCALL(xTask, __NR_eTaskGetState);
}

char *usr_pcTaskGetName(TaskHandle_t xTaskToQuery)
{
    return EXECUTE_SYSCALL(xTaskToQuery, __NR_pcTaskGetName);
}

TaskHandle_t usr_xTaskGetHandle(const char *pcNameToQuery)
{
    return EXECUTE_SYSCALL(pcNameToQuery, __NR_xTaskGetHandle);
}

TickType_t usr_xTaskGetTickCount(void)
{
    return EXECUTE_SYSCALL(__NR_xTaskGetTickCount);
}

TickType_t usr_xTaskGetTickCountFromISR(void)
{
    return EXECUTE_SYSCALL(__NR_xTaskGetTickCountFromISR);
}

BaseType_t usr_xTaskGetSchedulerState(void)
{
    return EXECUTE_SYSCALL(__NR_xTaskGetSchedulerState);
}

UBaseType_t usr_uxTaskGetNumberOfTasks(void)
{
    return EXECUTE_SYSCALL(__NR_uxTaskGetNumberOfTasks);
}

// Queue Management
QueueHandle_t usr_xQueueGenericCreate(uint32_t QueueLength, uint32_t ItemSize, uint8_t ucQueueType)
{
    return EXECUTE_SYSCALL(QueueLength, ItemSize, ucQueueType, __NR_xQueueGenericCreate);
}

void usr_vQueueDelete(QueueHandle_t xQueue)
{
    EXECUTE_SYSCALL(xQueue, __NR_vQueueDelete);
}

BaseType_t usr_xQueueGenericSend(QueueHandle_t xQueue, const void * const pvItemToQueue, TickType_t xTicksToWait, const BaseType_t xCopyPosition)
{
    return EXECUTE_SYSCALL(xQueue, pvItemToQueue, xTicksToWait, xCopyPosition, __NR_xQueueGenericSend);
}

BaseType_t usr_xQueueGenericSendFromISR(QueueHandle_t xQueue, const void * const pvItemToQueue, BaseType_t * const pxHigherPriorityTaskWoken, const BaseType_t xCopyPosition)
{
    return EXECUTE_SYSCALL(xQueue, pvItemToQueue, pxHigherPriorityTaskWoken, xCopyPosition, __NR_xQueueGenericSendFromISR);
}

BaseType_t usr_xQueueReceive(QueueHandle_t xQueue, void * const buffer, TickType_t TickstoWait)
{
    return EXECUTE_SYSCALL(xQueue, buffer, TickstoWait, __NR_xQueueReceive);
}

BaseType_t usr_xQueueReceiveFromISR(QueueHandle_t xQueue, void * const pvBuffer, BaseType_t * const pxHigherPriorityTaskWoken)
{
    return EXECUTE_SYSCALL(xQueue, pvBuffer, pxHigherPriorityTaskWoken, __NR_xQueueReceiveFromISR);
}

UBaseType_t usr_uxQueueMessagesWaiting(const QueueHandle_t xQueue)
{
    return EXECUTE_SYSCALL(xQueue, __NR_uxQueueMessagesWaiting);
}

UBaseType_t usr_uxQueueMessagesWaitingFromISR(const QueueHandle_t xQueue)
{
    return EXECUTE_SYSCALL(xQueue, __NR_uxQueueMessagesWaitingFromISR);
}

UBaseType_t usr_uxQueueSpacesAvailable(const QueueHandle_t xQueue)
{
    return EXECUTE_SYSCALL(xQueue, __NR_uxQueueSpacesAvailable);
}

BaseType_t usr_xQueueGenericReset(QueueHandle_t xQueue, BaseType_t xNewQueue)
{
    return EXECUTE_SYSCALL(xQueue, xNewQueue, __NR_xQueueGenericReset);
}

BaseType_t usr_xQueuePeek(QueueHandle_t xQueue, void * const pvBuffer, TickType_t xTicksToWait)
{
    return EXECUTE_SYSCALL(xQueue, pvBuffer, xTicksToWait, __NR_xQueuePeek);
}

BaseType_t usr_xQueuePeekFromISR(QueueHandle_t xQueue,  void * const pvBuffer)
{
    return EXECUTE_SYSCALL(xQueue, pvBuffer, __NR_xQueuePeekFromISR);
}

BaseType_t usr_xQueueIsQueueEmptyFromISR(const QueueHandle_t xQueue)
{
    return EXECUTE_SYSCALL(xQueue, __NR_xQueueIsQueueEmptyFromISR);
}

BaseType_t usr_xQueueIsQueueFullFromISR(const QueueHandle_t xQueue)
{
    return EXECUTE_SYSCALL(xQueue, __NR_xQueueIsQueueFullFromISR);
}

// Queue Sets
QueueSetHandle_t usr_xQueueCreateSet(const UBaseType_t uxEventQueueLength)
{
    return EXECUTE_SYSCALL(uxEventQueueLength, __NR_xQueueCreateSet);
}

BaseType_t usr_xQueueAddToSet(QueueSetMemberHandle_t xQueueOrSemaphore, QueueSetHandle_t xQueueSet)
{
    return EXECUTE_SYSCALL(xQueueOrSemaphore, xQueueSet, __NR_xQueueAddToSet);
}

BaseType_t usr_xQueueRemoveFromSet(QueueSetMemberHandle_t xQueueOrSemaphore, QueueSetHandle_t xQueueSet)
{
    return EXECUTE_SYSCALL(xQueueOrSemaphore, xQueueSet, __NR_xQueueRemoveFromSet);
}

QueueSetMemberHandle_t usr_xQueueSelectFromSet(QueueSetHandle_t xQueueSet, TickType_t const xTicksToWait)
{
    return EXECUTE_SYSCALL(xQueueSet, xTicksToWait, __NR_xQueueSelectFromSet);
}

QueueSetMemberHandle_t usr_xQueueSelectFromSetFromISR(QueueSetHandle_t xQueueSet)
{
    return EXECUTE_SYSCALL(xQueueSet, __NR_xQueueSelectFromSetFromISR);
}

// Semaphore
QueueHandle_t usr_xQueueCreateCountingSemaphore(const UBaseType_t uxMaxCount, const UBaseType_t uxInitialCount)
{
    return EXECUTE_SYSCALL(uxMaxCount, uxInitialCount, __NR_xQueueCreateCountingSemaphore);
}

QueueHandle_t usr_xQueueCreateMutex(const uint8_t ucQueueType)
{
    return EXECUTE_SYSCALL(ucQueueType, __NR_xQueueCreateMutex);
}

TaskHandle_t usr_xQueueGetMutexHolder(QueueHandle_t xSemaphore)
{
    return EXECUTE_SYSCALL(xSemaphore, __NR_xQueueGetMutexHolder);
}

BaseType_t usr_xQueueSemaphoreTake(QueueHandle_t xQueue, TickType_t xTicksToWait)
{
    return EXECUTE_SYSCALL(xQueue, xTicksToWait, __NR_xQueueSemaphoreTake);
}

BaseType_t usr_xQueueTakeMutexRecursive(QueueHandle_t xMutex, TickType_t xTicksToWait)
{
    return EXECUTE_SYSCALL(xMutex, xTicksToWait, __NR_xQueueTakeMutexRecursive);
}

BaseType_t usr_xQueueGiveMutexRecursive(QueueHandle_t xMutex)
{
    return EXECUTE_SYSCALL(xMutex, __NR_xQueueGiveMutexRecursive);
}

BaseType_t usr_xQueueGiveFromISR(QueueHandle_t xQueue, BaseType_t * const pxHigherPriorityTaskWoken)
{
    return EXECUTE_SYSCALL(xQueue, pxHigherPriorityTaskWoken, __NR_xQueueGiveFromISR);
}

TimerHandle_t usr_xTimerCreate(const char * const pcTimerName,
                                const TickType_t xTimerPeriodInTicks,
                                const UBaseType_t uxAutoReload,
                                void * const pvTimerID,
                                TimerCallbackFunction_t pxCallbackFunction)
{
    return EXECUTE_SYSCALL(pcTimerName, xTimerPeriodInTicks, uxAutoReload, pvTimerID, pxCallbackFunction, __NR_xTimerCreate);
}

BaseType_t usr_xTimerIsTimerActive(TimerHandle_t xTimer)
{
    return EXECUTE_SYSCALL(xTimer, __NR_xTimerIsTimerActive);
}

void *usr_pvTimerGetTimerID(const TimerHandle_t xTimer)
{
    return EXECUTE_SYSCALL(xTimer, __NR_pvTimerGetTimerID);
}

const char *usr_pcTimerGetName(TimerHandle_t xTimer)
{
    return EXECUTE_SYSCALL(xTimer, __NR_pcTimerGetName);
}

void usr_vTimerSetReloadMode(TimerHandle_t xTimer, const UBaseType_t uxAutoReload)
{
    EXECUTE_SYSCALL(xTimer, uxAutoReload, __NR_vTimerSetReloadMode);
}

BaseType_t usr_xTimerGenericCommand(TimerHandle_t xTimer, const BaseType_t xCommandID, const TickType_t xOptionalValue, BaseType_t * const pxHigherPriorityTaskWoken, const TickType_t xTicksToWait)
{
    return EXECUTE_SYSCALL(xTimer, xCommandID, xOptionalValue, pxHigherPriorityTaskWoken, xTicksToWait, __NR_xTimerGenericCommand);
}

void usr_vTimerSetTimerID(TimerHandle_t xTimer, void *pvNewID)
{
    EXECUTE_SYSCALL(xTimer, pvNewID, __NR_vTimerSetTimerID);
}

TickType_t usr_xTimerGetPeriod(TimerHandle_t xTimer)
{
    return EXECUTE_SYSCALL(xTimer, __NR_xTimerGetPeriod);
}

TickType_t usr_xTimerGetExpiryTime(TimerHandle_t xTimer)
{
    return EXECUTE_SYSCALL(xTimer, __NR_xTimerGetExpiryTime);
}

// Event Groups
EventGroupHandle_t usr_xEventGroupCreate(void)
{
    return EXECUTE_SYSCALL(__NR_xEventGroupCreate);
}

EventBits_t usr_xEventGroupWaitBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToWaitFor, const BaseType_t xClearOnExit, const BaseType_t xWaitForAllBits, TickType_t xTicksToWait)
{
    return EXECUTE_SYSCALL(xEventGroup, uxBitsToWaitFor, xClearOnExit, xWaitForAllBits, xTicksToWait, __NR_xEventGroupWaitBits);
}

EventBits_t usr_xEventGroupSetBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet)
{
    return EXECUTE_SYSCALL(xEventGroup, uxBitsToSet, __NR_xEventGroupSetBits);
}

EventBits_t usr_xEventGroupClearBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToClear)
{
    return EXECUTE_SYSCALL(xEventGroup, uxBitsToClear, __NR_xEventGroupClearBits);
}

EventBits_t usr_xEventGroupSync(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet, const EventBits_t uxBitsToWaitFor, TickType_t xTicksToWait)
{
    return EXECUTE_SYSCALL(xEventGroup, uxBitsToSet, uxBitsToWaitFor, xTicksToWait, __NR_xEventGroupSync);
}

void usr_vEventGroupDelete(EventGroupHandle_t xEventGroup)
{
    EXECUTE_SYSCALL(xEventGroup, __NR_vEventGroupDelete);
}

BaseType_t usr_xPortInIsrContext(void)
{
    return 0;
}

int usr_lwip_socket(int domain, int type, int protocol)
{
    return EXECUTE_SYSCALL(domain, type, protocol, __NR_lwip_socket);
}

int usr_lwip_connect(int s, const struct sockaddr *name, socklen_t namelen)
{
    return EXECUTE_SYSCALL(s, name, namelen, __NR_lwip_connect);
}

int usr_lwip_accept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
    return EXECUTE_SYSCALL(s, addr, addrlen, __NR_lwip_accept);
}

int usr_lwip_bind(int s, const struct sockaddr *name, socklen_t namelen)
{
    return EXECUTE_SYSCALL(s, name, namelen, __NR_lwip_bind);
}

int usr_lwip_shutdown(int s, int how)
{
    return EXECUTE_SYSCALL(s, how, __NR_lwip_shutdown);
}

int usr_lwip_getaddrinfo(const char *nodename, const char *servname, const struct addrinfo *hints, struct addrinfo **res)
{
    int ret;
    struct addrinfo *usr_addrinfo = malloc(NETDB_ELEM_SIZE);
    if (usr_addrinfo == NULL) {
        return -1;
    }
    *res = usr_addrinfo;
    ret = EXECUTE_SYSCALL(nodename, servname, hints, res, __NR_lwip_getaddrinfo);
    if (ret != 0) {
        free(usr_addrinfo);
        *res = NULL;
    }
    return ret;
}

int usr_lwip_getpeername(int s, struct sockaddr *name, socklen_t *namelen)
{
    return EXECUTE_SYSCALL(s, name, namelen, __NR_lwip_getpeername);
}

int usr_lwip_getsockname(int s, struct sockaddr *name, socklen_t *namelen)
{
    return EXECUTE_SYSCALL(s, name, namelen, __NR_lwip_getsockname);
}

int usr_lwip_setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen)
{
    return EXECUTE_SYSCALL(s, level, optname, optval, optlen, __NR_lwip_setsockopt);
}

int usr_lwip_getsockopt(int s, int level, int optname, void *optval, socklen_t *optlen)
{
    return EXECUTE_SYSCALL(s, level, optname, optval, optlen, __NR_lwip_getsockopt);
}

int usr_lwip_listen(int s, int backlog)
{
    return EXECUTE_SYSCALL(s, backlog, __NR_lwip_listen);
}

size_t usr_lwip_recv(int s, void *mem, size_t len, int flags)
{
    return EXECUTE_SYSCALL(s, mem, len, flags, __NR_lwip_recv);
}

ssize_t usr_lwip_recvmsg(int s, struct msghdr *message, int flags)
{
    return EXECUTE_SYSCALL(s, message, flags, __NR_lwip_recvmsg);
}

ssize_t usr_lwip_recvfrom(int s, void *mem, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen)
{
    return EXECUTE_SYSCALL(s, mem, len, flags, from, fromlen, __NR_lwip_recvfrom);
}

size_t usr_lwip_send(int s, const void *data, size_t size, int flags)
{
    return EXECUTE_SYSCALL(s, data, size, flags, __NR_lwip_send);
}

ssize_t usr_lwip_sendmsg(int s, const struct msghdr *msg, int flags)
{
    return EXECUTE_SYSCALL(s, msg, flags, __NR_lwip_sendmsg);
}

ssize_t usr_lwip_sendto(int s, const void *data, size_t size, int flags, const struct sockaddr *to, socklen_t tolen)
{
    return EXECUTE_SYSCALL(s, data, size, flags, to, tolen, __NR_lwip_sendto);
}

const char *usr_lwip_inet_ntop(int af, const void *src, char *dst, socklen_t size)
{
    return EXECUTE_SYSCALL(af, src, dst, size, __NR_lwip_inet_ntop);
}

int usr_lwip_inet_pton(int af, const char *src, void *dst)
{
    return EXECUTE_SYSCALL(af, src, dst, __NR_lwip_inet_pton);
}

u32_t usr_lwip_htonl(u32_t n)
{
    return EXECUTE_SYSCALL(n, __NR_lwip_htonl);
}

u16_t usr_lwip_htons(u16_t n)
{
    return EXECUTE_SYSCALL(n, __NR_lwip_htons);
}

int *usr___errno(void)
{
    return EXECUTE_SYSCALL(__NR___errno);
}

int usr_open(const char *path, int flags, int mode)
{
    return EXECUTE_SYSCALL(path, flags, mode, __NR_open);
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

int usr_select(int maxfdp1, fd_set *readset, fd_set *writeset, fd_set *exceptset, struct timeval *timeout)
{
    return EXECUTE_SYSCALL(maxfdp1, readset, writeset, exceptset, timeout, __NR_select);
}

int usr_poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    return EXECUTE_SYSCALL(fds, nfds, timeout, __NR_poll);
}

int usr_ioctl(int s, long cmd, void *argp)
{
    return EXECUTE_SYSCALL(s, cmd, argp, __NR_ioctl);
}

int usr_fcntl(int s, int cmd, int val)
{
    return EXECUTE_SYSCALL(s, cmd, val, __NR_fcntl);
}

esp_err_t usr_gpio_config(const gpio_config_t *conf)
{
    return EXECUTE_SYSCALL(conf, __NR_gpio_config);
}

esp_err_t usr_gpio_install_isr_service(int intr_alloc_flags)
{
    return EXECUTE_SYSCALL(intr_alloc_flags, __NR_gpio_install_isr_service);
}

esp_err_t gpio_softisr_handler_add(gpio_num_t gpio_num, gpio_isr_t isr_handler, void *args, usr_gpio_handle_t *gpio_handle)
{
    return EXECUTE_SYSCALL(gpio_num, isr_handler, args, gpio_handle, __NR_gpio_softisr_handler_add);
}

esp_err_t gpio_softisr_handler_remove(usr_gpio_handle_t gpio_handle)
{
    return EXECUTE_SYSCALL(gpio_handle, __NR_gpio_softisr_handler_remove);
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

esp_err_t usr_esp_event_handler_instance_register(esp_event_base_t event_base,
                                              int32_t event_id,
                                              esp_event_handler_t event_handler,
                                              void *event_handler_arg,
                                              esp_event_handler_instance_t *context)
{
    return EXECUTE_SYSCALL(event_base, event_id, event_handler, event_handler_arg, context,
                          __NR_esp_event_handler_instance_register);
}

esp_err_t usr_esp_event_handler_instance_unregister(esp_event_base_t event_base,
                                                int32_t event_id,
                                                esp_event_handler_instance_t context)
{
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

UIRAM_ATTR uint32_t usr_esp_random(void)
{
    return EXECUTE_SYSCALL(__NR_esp_random);
}

void usr_esp_fill_random(void *buf, size_t len)
{
    EXECUTE_SYSCALL(buf, len, __NR_esp_fill_random);
}

esp_err_t usr_esp_timer_create(const esp_timer_create_args_t* create_args, esp_timer_handle_t* out_handle)
{
    return EXECUTE_SYSCALL(create_args, out_handle, __NR_esp_timer_create);
}

esp_err_t usr_esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us)
{
    return EXECUTE_SYSCALL(timer, timeout_us, __NR_esp_timer_start_once);
}

esp_err_t usr_esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period)
{
    return EXECUTE_SYSCALL(timer, period, __NR_esp_timer_start_periodic);
}

esp_err_t usr_esp_timer_stop(esp_timer_handle_t timer)
{
    return EXECUTE_SYSCALL(timer, __NR_esp_timer_stop);
}

esp_err_t usr_esp_timer_delete(esp_timer_handle_t timer)
{
    esp_err_t err = EXECUTE_SYSCALL(timer, __NR_esp_timer_delete);
    return err;
}

UIRAM_ATTR int64_t usr_esp_timer_get_time(void)
{
    return EXECUTE_SYSCALL(__NR_esp_timer_get_time);
}

UIRAM_ATTR int64_t usr_esp_timer_get_next_alarm(void)
{
    return EXECUTE_SYSCALL(__NR_esp_timer_get_next_alarm);
}

bool usr_esp_timer_is_active(esp_timer_handle_t timer)
{
    return EXECUTE_SYSCALL(timer, __NR_esp_timer_is_active);
}

int64_t UIRAM_ATTR usr_esp_system_get_time(void)
{
    return EXECUTE_SYSCALL(__NR_esp_system_get_time);
}

esp_err_t usr_uart_driver_install(uart_port_t uart_num, int rx_buffer_size, int tx_buffer_size, int queue_size, QueueHandle_t* uart_queue, int intr_alloc_flags)
{
    return EXECUTE_SYSCALL(uart_num, rx_buffer_size, tx_buffer_size, queue_size, uart_queue, intr_alloc_flags, __NR_uart_driver_install);
}

esp_err_t usr_uart_driver_delete(uart_port_t uart_num)
{
    return EXECUTE_SYSCALL(uart_num, __NR_uart_driver_delete);
}

int usr_uart_write_bytes(uart_port_t uart_num, const void* src, size_t size)
{
    return EXECUTE_SYSCALL(uart_num, src, size, __NR_uart_write_bytes);
}

int usr_uart_read_bytes(uart_port_t uart_num, void* buf, uint32_t length, TickType_t ticks_to_wait)
{
    return EXECUTE_SYSCALL(uart_num, buf, length, ticks_to_wait, __NR_uart_read_bytes);
}

UIRAM_ATTR bool usr_spi_flash_cache_enabled(void)
{
    // Flash cache will always be enabled in user app.
    return true;
}

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

void usr_lwip_freeaddrinfo(struct addrinfo *ai)
{
    free(ai);
}

int usr_vprintf(const char * fmt, va_list ap)
{
    int ret;
    char *str_ptr = NULL;
    ret = vasprintf(&str_ptr, fmt, ap);
    assert(str_ptr != NULL);
    ret = usr_write_log_line(str_ptr, ret);
    free(str_ptr);
    return ret;
}

int usr_printf(const char *fmt, ...)
{
    int ret = 0;
    char *str_ptr = NULL;
    va_list ap = 0;
    va_start(ap, fmt);
    if (_is_heap_initialized) {
        ret = vasprintf(&str_ptr, fmt, ap);
        assert(str_ptr != NULL);
    } else {
        char buff[256];
        ret = vsnprintf(buff, sizeof(buff), fmt, ap);
        str_ptr = buff;
    }
    va_end(ap);
    ret = usr_write_log_line(str_ptr, ret);
    if (_is_heap_initialized) {
        free(str_ptr);
    }
    return ret;
}

int usr_ets_printf(const char *fmt, ...)
{
    int ret = 0;
    char *str_ptr = NULL;
    va_list ap = 0;
    va_start(ap, fmt);
    if (_is_heap_initialized) {
        ret = vasprintf(&str_ptr, fmt, ap);
        assert(str_ptr != NULL);
    } else {
        char buff[256];
        ret = vsnprintf(buff, sizeof(buff), fmt, ap);
        str_ptr = buff;
    }
    va_end(ap);
    ret = usr_write_log_line_unlocked(str_ptr, ret);
    if (_is_heap_initialized) {
        free(str_ptr);
    }
    return ret;
}
