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

#include "esp_rom_md5.h"

#ifdef CONFIG_IDF_TARGET_ESP32C3
#include "esp32c3/rom/ets_sys.h"
#endif

#ifndef XTSTR
#define _XTSTR(x)	# x
#define XTSTR(x)	_XTSTR(x)
#endif

extern int _user_bss_start;
extern int _user_bss_end;
extern int _user_heap_start;
extern int _user_data_start;
extern void user_main(void);

#define DISPATCHER_TASK_PRIO 22
#define DISPATCHER_TASK_STACK_SIZE 1024

/* Queue to receive registered events from protected app */
static QueueHandle_t usr_dispatcher_queue;
/* Task to trigger event callbacks in user app */
static TaskHandle_t usr_dispatcher_task_handle;

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

/* Embed _user_data_start and _user_heap_start as custom_app_desc in the user_app binary.
 * Due to its fixed offset (sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)),
 * it can be parsed by protected app to add the heap region for user app
 */
const __attribute__((section(".rodata_custom_desc"))) usr_custom_app_desc_t custom_app_desc = {
    .user_app_dram_start = (int)&_user_data_start,
    .user_app_heap_start = (int)&_user_heap_start,
};

static void usr_dispatcher_task(void *arg);

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

UIRAM_ATTR void usr__lock_acquire(_lock_t *lock)
{
    EXECUTE_SYSCALL(lock, __NR__lock_acquire);
}

UIRAM_ATTR void usr__lock_release(_lock_t *lock)
{
    EXECUTE_SYSCALL(lock, __NR__lock_release);
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
TaskHandle_t usr_xTaskCreatePinnedToCore(TaskFunction_t pvTaskCode,
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
BaseType_t usr_xTaskGenericNotify(TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction, uint32_t *pulPreviousNotificationValue)
{
    return EXECUTE_SYSCALL(xTaskToNotify, ulValue, eAction, pulPreviousNotificationValue, __NR_xTaskGenericNotify);
}

BaseType_t usr_xTaskGenericNotifyFromISR(TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction, uint32_t *pulPreviousNotificationValue, BaseType_t *pxHigherPriorityTaskWoken)
{
    return EXECUTE_SYSCALL(xTaskToNotify, ulValue, eAction, pulPreviousNotificationValue, pxHigherPriorityTaskWoken, __NR_xTaskGenericNotifyFromISR);
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
    return EXECUTE_SYSCALL(pcTimerName, xTimerPeriodInTicks, uxAutoReload, pvTimerID, pxCallbackFunction, usr_dispatcher_queue, __NR_xTimerCreate);
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
    return EXECUTE_SYSCALL(nodename, servname, hints, res, __NR_lwip_getaddrinfo);
}

void usr_lwip_freeaddrinfo(struct addrinfo *ai)
{
    EXECUTE_SYSCALL(ai, __NR_lwip_freeaddrinfo);
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

UIRAM_ATTR static void usr_dispatcher_task(void *arg)
{
    QueueHandle_t usr_queue = (QueueHandle_t)arg;
    usr_dispatch_ctx_t dispatch_ctx;
    while (1) {
        usr_xQueueReceive(usr_queue, &dispatch_ctx, portMAX_DELAY);
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
                usr_esp_event_handler_t usr_event_handler = event_args->usr_context.usr_event_handler;
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

esp_err_t gpio_softisr_handler_add(gpio_num_t gpio_num, gpio_isr_t isr_handler, void *args, usr_gpio_handle_t *gpio_handle)
{
    return EXECUTE_SYSCALL(gpio_num, isr_handler, args, gpio_handle, usr_dispatcher_queue, __NR_gpio_softisr_handler_add);
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

esp_err_t usr_esp_event_handler_instance_register(usr_esp_event_base_t event_base,
                                              int32_t event_id,
                                              usr_esp_event_handler_t event_handler,
                                              void *event_handler_arg,
                                              usr_esp_event_handler_instance_t *context)
{
    return EXECUTE_SYSCALL(event_base, event_id, event_handler, event_handler_arg, context, usr_dispatcher_queue,
                          __NR_esp_event_handler_instance_register);
}

esp_err_t usr_esp_event_handler_instance_unregister(usr_esp_event_base_t event_base,
                                                int32_t event_id,
                                                usr_esp_event_handler_instance_t context)
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

void *usr_malloc(size_t size)
{
    return EXECUTE_SYSCALL(size, __NR_malloc);
}

void *usr_calloc(size_t nmemb, size_t size)
{
    return EXECUTE_SYSCALL(nmemb, size, __NR_calloc);
}

void usr_free(void *ptr)
{
    EXECUTE_SYSCALL(ptr, __NR_free);
}

void *usr_realloc(void* ptr, size_t size)
{
    return EXECUTE_SYSCALL(ptr, size, __NR_realloc);
}

void *usr_heap_caps_malloc( size_t size, uint32_t caps )
{
    return usr_malloc(size);
}

void *usr_heap_caps_calloc( size_t n, size_t size, uint32_t caps)
{
    return usr_calloc(n, size);
}

void usr_heap_caps_free(void *ptr)
{
    usr_free(ptr);
}

UIRAM_ATTR uint32_t usr_esp_random(void)
{
    return EXECUTE_SYSCALL(__NR_esp_random);
}

void usr_esp_fill_random(void *buf, size_t len)
{
    EXECUTE_SYSCALL(buf, len, __NR_esp_fill_random);
}

esp_err_t usr_esp_timer_create(const esp_timer_create_args_t* create_args,
        esp_timer_handle_t* out_handle, QueueHandle_t usr_queue)
{
    return EXECUTE_SYSCALL(create_args, out_handle, usr_dispatcher_queue, __NR_esp_timer_create);
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

UIRAM_ATTR bool usr_spi_flash_cache_enabled(void)
{
    // Flash cache will always be enabled in user app.
    return true;
}

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

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
    int ret;
    char *str_ptr = NULL;
    va_list ap = 0;
    va_start(ap, fmt);
    ret = vasprintf(&str_ptr, fmt, ap);
    assert(str_ptr != NULL);
    va_end(ap);
    ret = usr_write_log_line(str_ptr, ret);
    free(str_ptr);
    return ret;
}

int usr_ets_printf(const char *fmt, ...)
{
    int ret;
    char *str_ptr = NULL;
    va_list ap = 0;
    va_start(ap, fmt);
    ret = vasprintf(&str_ptr, fmt, ap);
    assert(str_ptr != NULL);
    va_end(ap);
    ret = usr_write_log_line_unlocked(str_ptr, ret);
    free(str_ptr);
    return ret;
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

void _user_main()
{
    usr_clear_bss();
    usr_dispatcher_queue = usr_xQueueGenericCreate(10, sizeof(usr_dispatch_ctx_t), queueQUEUE_TYPE_BASE);
    if (!usr_dispatcher_queue) {
        usr_printf("Failed to create dispatcher queue\n");
        abort();
    }
    usr_xTaskCreatePinnedToCore(usr_dispatcher_task, "User event dispatcher", DISPATCHER_TASK_STACK_SIZE, usr_dispatcher_queue, DISPATCHER_TASK_PRIO, &usr_dispatcher_task_handle, 0);
    if (!usr_dispatcher_task_handle) {
        usr_printf("Failed to create dispatcher task");
        usr_vQueueDelete(usr_dispatcher_queue);
        abort();
    }
    user_main();
    usr_vTaskDelete(NULL);
}
