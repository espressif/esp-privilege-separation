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
#include "string.h"
#include "esp_idf_version.h"
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
#include "syscall_structs.h"
#include "esp_map.h"

#include <lwip/sockets.h>
#include <lwip/netdb.h>

#include "soc_defs.h"

#include "esp_rom_md5.h"

#include <esp_spi_flash.h>
#include <esp_partition.h>
#include <esp_rom_uart.h>

#ifdef CONFIG_IDF_TARGET_ESP32C3
#include "esp32c3/rom/ets_sys.h"
#endif

#if CONFIG_IDF_TARGET_ARCH_RISCV
#include "riscv/interrupt.h"
#include "riscv/rvruntime-frames.h"
#include "ecall_context.h"
#endif

#include <driver/uart.h>

#define TAG     __func__

typedef void (*syscall_t)(void);
typedef void* usr_gpio_handle_t;

static const char *USER_TIMER_STRING = "user_timer";

/* These are the indexes in esp_event_map_arr to store the user space EVENT base.
 * This is required because esp_event_base_t is a string pointer and it is different for protected space
 * as well as user space, so if the user wants to register an event handler for an event handled in protected space,
 * we need to create a mapping for these string pointers and translate them
 */
typedef enum {
    WIFI_EVENT_INDEX = 0,
    IP_EVENT_INDEX,
    ESP_EVENT_MAX_INDEX
} esp_event_index_t;

static esp_event_base_t esp_event_map_arr[ESP_EVENT_MAX_INDEX];

static DRAM_ATTR int usr_dispatcher_queue_index;
static DRAM_ATTR int usr_mem_cleanup_queue_index;
static int uart_driver_queue_index[UART_NUM_MAX];

static DRAM_ATTR QueueHandle_t usr_dispatcher_queue_handle;
static DRAM_ATTR QueueHandle_t usr_mem_cleanup_queue_handle;

void esp_time_impl_set_boot_time(uint64_t time_us);
uint64_t esp_time_impl_get_boot_time(void);
int64_t esp_system_get_time(void);
int *__real___errno(void);

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

int sys_ni_syscall(void)
{
    return -1;
}

int sys_putchar(int c)
{
    return putchar(c);
}

int sys_write_log_line(const char *str, size_t len)
{
    if (!is_valid_user_d_addr((void *)str) ||
        !is_valid_user_d_addr((void *)str + len)) {
        return -1;
    }
    return fwrite(str, len, 1, __getreent()->_stdout);
}

int sys_write_log_line_unlocked(const char *str, size_t len)
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

uint32_t sys_ets_get_cpu_frequency(void)
{
    return ets_get_cpu_frequency();
}

void sys_esp_rom_md5_init(md5_context_t *context)
{
    if (!is_valid_udram_addr((void *)context)) {
        return;
    }
    esp_rom_md5_init(context);
}

void sys_esp_rom_md5_update(md5_context_t *context, const void *buf, uint32_t len)
{
    if (!is_valid_udram_addr((void *)context) ||
        !is_valid_user_d_addr((void *)buf)) {
        return;
    }
    esp_rom_md5_update(context, buf, len);
}

void sys_esp_rom_md5_final(uint8_t *digest, md5_context_t *context)
{
    if (!is_valid_udram_addr((void *)digest) ||
        !is_valid_udram_addr((void *)context)) {
        return;
    }
    esp_rom_md5_final(digest, context);
}

int sys_esp_rom_uart_tx_one_char(uint8_t c)
{
    return esp_rom_uart_tx_one_char(c);
}

int sys_esp_rom_uart_rx_one_char(uint8_t *c)
{
    if (!is_valid_udram_addr(c)) {
        return -1;
    }
    return esp_rom_uart_rx_one_char(c);
}

IRAM_ATTR void sys__lock_acquire(_lock_t *lock)
{
    if (!is_valid_udram_addr(lock)) {
        return;
    }
    _lock_acquire(lock);
}

IRAM_ATTR void sys__lock_acquire_recursive(_lock_t *lock)
{
    if (!is_valid_udram_addr(lock)) {
        return;
    }
    _lock_acquire_recursive(lock);
}

IRAM_ATTR int sys__lock_try_acquire(_lock_t *lock)
{
    if (!is_valid_udram_addr(lock)) {
        return -1;
    }
    return _lock_try_acquire(lock);
}

IRAM_ATTR int sys__lock_try_acquire_recursive(_lock_t *lock)
{
    if (!is_valid_udram_addr(lock)) {
        return -1;
    }
    return _lock_try_acquire_recursive(lock);
}

IRAM_ATTR void sys__lock_release(_lock_t *lock)
{
    if (!is_valid_udram_addr(lock)) {
        return;
    }
    _lock_release(lock);
}

IRAM_ATTR void sys__lock_release_recursive(_lock_t *lock)
{
    if (!is_valid_udram_addr(lock)) {
        return;
    }
    _lock_release_recursive(lock);
}

IRAM_ATTR void sys___retarget_lock_acquire(_LOCK_T lock)
{
    if (!is_valid_udram_addr(lock)) {
        return;
    }
    __retarget_lock_acquire(lock);
}

IRAM_ATTR void sys___retarget_lock_acquire_recursive(_LOCK_T lock)
{
    if (!is_valid_udram_addr(lock)) {
        return;
    }
    __retarget_lock_acquire_recursive(lock);
}

IRAM_ATTR int sys___retarget_lock_try_acquire(_LOCK_T lock)
{
    if (!is_valid_udram_addr(lock)) {
        return -1;
    }
    return __retarget_lock_try_acquire(lock);
}

IRAM_ATTR int sys___retarget_lock_try_acquire_recursive(_LOCK_T lock)
{
    if (!is_valid_udram_addr(lock)) {
        return -1;
    }
    return __retarget_lock_try_acquire_recursive(lock);
}

IRAM_ATTR void sys___retarget_lock_release_recursive(_LOCK_T lock)
{
    if (!is_valid_udram_addr(lock)) {
        return;
    }
    __retarget_lock_release_recursive(lock);
}

IRAM_ATTR void sys___retarget_lock_release(_LOCK_T lock)
{
    if (!is_valid_udram_addr(lock)) {
        return;
    }
    __retarget_lock_release(lock);
}

void sys_esp_time_impl_set_boot_time(uint64_t time_us)
{
    esp_time_impl_set_boot_time(time_us);
}

uint64_t sys_esp_time_impl_get_boot_time(void)
{
    return esp_time_impl_get_boot_time();
}

int sys_xTaskCreate(TaskFunction_t pvTaskCode,
                                   const char * const pcName,
                                   void * const pvParameters,
                                   UBaseType_t uxPriority,
                                   const BaseType_t xCoreID,
                                   usr_task_ctx_t *task_ctx)
{
    static bool _is_user_app_up;

    if (!is_valid_user_i_addr(pvTaskCode)) {
        ESP_LOGE(TAG, "Incorrect address of user function");
        return pdFAIL;
    }

    /* The first user task will be spawn from the protected application so
     * the name and the task context will be from protected space
     */
    if (_is_user_app_up) {
        if (!is_valid_udram_addr((void *)task_ctx)) {
            ESP_LOGE(TAG, "Invalid user task context %p", task_ctx);
            return pdFAIL;
        }

        if (!is_valid_user_d_addr((void *)pcName) ||
                !is_valid_user_d_addr((void *)((int)pcName + CONFIG_FREERTOS_MAX_TASK_NAME_LEN))) {
            ESP_LOGE(TAG, "Invalid user task name");
            return pdFAIL;
        }
    }

    TaskHandle_t handle;
    StaticTask_t *xtaskTCB = NULL;
    StackType_t *xtaskStack = NULL, *kernel_stack = NULL;
    int *usr_errno = NULL;
    int err = pdPASS;

    xtaskTCB = heap_caps_malloc(sizeof(StaticTask_t), portTcbMemoryCaps);
    if (xtaskTCB == NULL) {
        ESP_LOGE(TAG, "Insufficient memory for TCB");
        return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
    }

    const int stack_size = task_ctx->stack_size;

    xtaskStack = (StackType_t *)task_ctx->stack;
    if (!is_valid_udram_addr((void *)xtaskStack) || !is_valid_udram_addr((void *)((int)xtaskStack + stack_size))) {
        ESP_LOGE(TAG, "Invalid memory for user stack");
        err = pdFAIL;
        goto failure;
    }

    kernel_stack = heap_caps_malloc(KERNEL_STACK_SIZE, portStackMemoryCaps);
    if (kernel_stack == NULL) {
        ESP_LOGE(TAG, "Insufficient memory for kernel stack");
        err = errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
        goto failure;
    }
    memset(kernel_stack, tskSTACK_FILL_BYTE, KERNEL_STACK_SIZE * sizeof(StackType_t));

    usr_errno = task_ctx->task_errno;
    if (!is_valid_udram_addr(usr_errno)) {
        ESP_LOGE(TAG, "Invalid memory for user errno");
        err = pdFAIL;
        goto failure;
    }

    *usr_errno = 0;

    int wrapper_index = esp_map_add(NULL, ESP_MAP_TASK_ID);
    if (!wrapper_index) {
        goto failure;
    }
    esp_map_handle_t *wrapper_handle = esp_map_get_handle(wrapper_index);

    /* Suspend the scheduler to ensure that it does not switch to the newly created task.
     * We need to first set the kernel stack as a TLS and only then it should be executed
     */
    vTaskSuspendAll();
    handle = xTaskCreateStaticPinnedToCore(pvTaskCode, pcName, stack_size, pvParameters, uxPriority, xtaskStack, xtaskTCB, xCoreID);

    wrapper_handle->handle = handle;

    // The 1st (0th index) TLS pointer is used by pthread
    // 2nd TLS pointer is used to store the kernel stack, used when servicing system calls
    // 3rd TLS pointer is used to store the WORLD of the task. 0 = WORLD0 and 1 = WORLD1
    // 4th TLS pointer is used to store the errno variable
    // 5th TLS pointer is used to store the wrapper_index for task handle
    vTaskSetThreadLocalStoragePointerAndDelCallback(handle, ESP_PA_TLS_OFFSET_KERN_STACK, kernel_stack, NULL);
    vTaskSetThreadLocalStoragePointerAndDelCallback(handle, ESP_PA_TLS_OFFSET_WORLD, (void *)1, NULL);
    vTaskSetThreadLocalStoragePointerAndDelCallback(handle, ESP_PA_TLS_OFFSET_ERRNO, usr_errno, NULL);
    vTaskSetThreadLocalStoragePointerAndDelCallback(handle, ESP_PA_TLS_OFFSET_SHIM_HANDLE, (void *)wrapper_index, NULL);
    xTaskResumeAll();

    if (is_valid_udram_addr(task_ctx->task_handle)) {
        *(TaskHandle_t *)(task_ctx->task_handle) = (TaskHandle_t)wrapper_index;
    }

    if (!_is_user_app_up) {
        _is_user_app_up = 1;
    };

    return err;
failure:
    if (xtaskTCB) {
        free(xtaskTCB);
    }
    if (kernel_stack) {
        free(kernel_stack);
    }
    return err;
}

void vPortCleanUpTCB (void *pxTCB)
{
    if (pvTaskGetThreadLocalStoragePointer(pxTCB, ESP_PA_TLS_OFFSET_KERN_STACK) == NULL) {
        /* This means this task is a kernel task.
         * User task has a kernel stack at this index
         * Do nothing and return*/
        return;
    }

    void *usr_ptr;
    void *curr_stack = pxTaskGetStackStart(pxTCB);

    int wrapper_index = (int)pvTaskGetThreadLocalStoragePointer(pxTCB, ESP_PA_TLS_OFFSET_SHIM_HANDLE);
    esp_map_remove(wrapper_index);

    if (is_valid_udram_addr(curr_stack)) {
        /* The stack is the user space stack. Free the kernel space stack */
        void *k_stack = pvTaskGetThreadLocalStoragePointer(pxTCB, ESP_PA_TLS_OFFSET_KERN_STACK);
        free(k_stack);
        usr_ptr = curr_stack;
    } else {
        /* The task might be deleted when it is executing system-call, in that case, the stack point will point to kernel stack.
         * Retrieve the user stack from system call stack frame
         */
#if CONFIG_IDF_TARGET_ARCH_XTENSA
        XtSyscallExcFrame *syscall_stack = (XtSyscallExcFrame *)((uint32_t)pxTaskGetStackStart(pxTCB) + KERNEL_STACK_SIZE - XT_ISTK_FRMSZ);
        free((void *)syscall_stack->user_stack);
#elif CONFIG_IDF_TARGET_ARCH_RISCV
        RvEcallFrame *syscall_stack = (RvEcallFrame *)((uint32_t)pxTaskGetStackStart(pxTCB) + KERNEL_STACK_SIZE - RV_ESTK_FRMSZ);
        usr_ptr = (void *)syscall_stack->stack;
        free(curr_stack);
#endif
    }

    if (usr_mem_cleanup_queue_handle) {
        // Send user space stack
        xQueueSend(usr_mem_cleanup_queue_handle, &usr_ptr, 0);
        // Send user space errno variable
        usr_ptr = pvTaskGetThreadLocalStoragePointer(pxTCB, ESP_PA_TLS_OFFSET_ERRNO);
        xQueueSend(usr_mem_cleanup_queue_handle, &usr_ptr, 0);
    }

    /* prvDeleteTCB accesses TCB members after this function returns so to avoid use-after-free case,
     * change the ucStaticallyAllocated field such that prvDeleteTCB will free the TCB
     */
    ((StaticTask_t *)pxTCB)->uxDummy20 = 1;
}

void sys_vTaskDelete(TaskHandle_t TaskHandle)
{
    if (TaskHandle == NULL) {
        return vTaskDelete(NULL);
    }
    int wrapper_index = (int)TaskHandle;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_TASK_ID);
    if (wrapper_handle == NULL) {
        return;
    }
    return vTaskDelete((TaskHandle_t)wrapper_handle->handle);
}

void sys_vTaskDelay(TickType_t TickstoWait)
{
    vTaskDelay(TickstoWait);
}

void sys_vTaskDelayUntil(TickType_t * const pxPreviousWakeTime, const TickType_t xTimeIncrement)
{
    if (!is_valid_udram_addr(pxPreviousWakeTime)) {
        return;
    }
    vTaskDelayUntil(pxPreviousWakeTime, xTimeIncrement);
}

UBaseType_t sys_uxTaskPriorityGet(const TaskHandle_t xTask)
{
    if (xTask == NULL) {
        return uxTaskPriorityGet(NULL);
    }
    int wrapper_index = (int)xTask;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_TASK_ID);
    if (wrapper_handle == NULL) {
        return -1;
    }
    return uxTaskPriorityGet((TaskHandle_t)wrapper_handle->handle);
}

void sys_vTaskPrioritySet(TaskHandle_t xTask, UBaseType_t uxNewPriority)
{
    if (xTask == NULL) {
        return vTaskPrioritySet(xTask, uxNewPriority);
    }
    int wrapper_index = (int)xTask;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_TASK_ID);
    if (wrapper_handle == NULL) {
        return;
    }
    vTaskPrioritySet((TaskHandle_t)wrapper_handle->handle, uxNewPriority);
}

UBaseType_t sys_uxTaskPriorityGetFromISR(const TaskHandle_t xTask)
{
    int wrapper_index = (int)xTask;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_TASK_ID);
    if (wrapper_handle == NULL) {
        return -1;
    }
    return uxTaskPriorityGetFromISR((TaskHandle_t)wrapper_handle->handle);
}

void sys_vTaskSuspend(TaskHandle_t xTaskToSuspend)
{
    if (xTaskToSuspend == NULL) {
        return vTaskSuspend(NULL);
    }
    int wrapper_index = (int)xTaskToSuspend;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_TASK_ID);
    if (wrapper_handle == NULL) {
        return;
    }
    vTaskSuspend((TaskHandle_t)wrapper_handle->handle);
}

void sys_vTaskResume(TaskHandle_t xTaskToResume)
{
    int wrapper_index = (int)xTaskToResume;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_TASK_ID);
    if (wrapper_handle == NULL) {
        return;
    }
    vTaskResume((TaskHandle_t)wrapper_handle->handle);
}

BaseType_t sys_xTaskResumeFromISR(TaskHandle_t xTaskToResume)
{
    int wrapper_index = (int)xTaskToResume;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_TASK_ID);
    if (wrapper_handle == NULL) {
        return -1;
    }
    return xTaskResumeFromISR((TaskHandle_t)wrapper_handle->handle);
}

BaseType_t sys_xTaskAbortDelay(TaskHandle_t xTask)
{
#if INCLUDE_xTaskAbortDelay
    if (xTask == NULL) {
        xTaskAbortDelay(NULL);
    }
    int wrapper_index = (int)xTask;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_TASK_ID);
    if (wrapper_handle == NULL) {
        return -1;
    }
    return xTaskAbortDelay((TaskHandle_t)wrapper_handle->handle);
#endif
    return 0;
}

void sys_vPortYield(void)
{
    vPortYield();
}

void sys_vPortEnterCritical(void)
{
    vPortEnterCritical();
}

void sys_vPortExitCritical(void)
{
    vPortExitCritical();
}

int sys_vPortSetInterruptMask(void)
{
    return vPortSetInterruptMask();
}

void sys_vPortClearInterruptMask(int mask)
{
    vPortClearInterruptMask(mask);
}

void sys_vTaskSuspendAll(void)
{
    vTaskSuspendAll();
}

BaseType_t sys_xTaskResumeAll(void)
{
    return xTaskResumeAll();
}

void sys_vTaskStepTick(const TickType_t xTicksToJump)
{
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
    vTaskStepTick(xTicksToJump);
#endif
}

#if defined(IDF_VERSION_V4_3)
BaseType_t sys_xTaskGenericNotify(TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction, uint32_t *pulPreviousNotificationValue)
#elif defined(IDF_VERSION_V4_4)
BaseType_t sys_xTaskGenericNotify(TaskHandle_t xTaskToNotify, UBaseType_t uxIndexToNotify, uint32_t ulValue, eNotifyAction eAction, uint32_t *pulPreviousNotificationValue)
#endif
{
    if (!is_valid_udram_addr(pulPreviousNotificationValue)) {
        return pdFAIL;
    }

    int wrapper_index = (int)xTaskToNotify;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_TASK_ID);
    if (wrapper_handle == NULL) {
        return 0;
    }
#if defined(IDF_VERSION_V4_3)
    return xTaskGenericNotify((TaskHandle_t)wrapper_handle->handle, ulValue, eAction, pulPreviousNotificationValue);
#elif defined(IDF_VERSION_V4_4)
    return xTaskGenericNotify((TaskHandle_t)wrapper_handle->handle, uxIndexToNotify, ulValue, eAction, pulPreviousNotificationValue);
#endif
}

#if defined(IDF_VERSION_V4_3)
BaseType_t sys_xTaskGenericNotifyFromISR(TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction, uint32_t *pulPreviousNotificationValue, BaseType_t *pxHigherPriorityTaskWoken)
#elif defined(IDF_VERSION_V4_4)
BaseType_t sys_xTaskGenericNotifyFromISR(TaskHandle_t xTaskToNotify, UBaseType_t uxIndexToNotify, uint32_t ulValue, eNotifyAction eAction, uint32_t *pulPreviousNotificationValue, BaseType_t *pxHigherPriorityTaskWoken)
#endif
{
    if (!is_valid_udram_addr(pulPreviousNotificationValue)) {
        return pdFAIL;
    }

    if (!is_valid_udram_addr(pxHigherPriorityTaskWoken) && pxHigherPriorityTaskWoken != NULL) {
        return pdFAIL;
    }

    int wrapper_index = (int)xTaskToNotify;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_TASK_ID);
    if (wrapper_handle == NULL) {
        return -1;
    }
#if defined(IDF_VERSION_V4_3)
    return xTaskGenericNotifyFromISR((TaskHandle_t)wrapper_handle->handle, ulValue, eAction, pulPreviousNotificationValue, pxHigherPriorityTaskWoken);
#elif defined(IDF_VERSION_V4_4)
    return xTaskGenericNotifyFromISR((TaskHandle_t)wrapper_handle->handle, uxIndexToNotify, ulValue, eAction, pulPreviousNotificationValue, pxHigherPriorityTaskWoken);
#endif
}

void sys_vTaskNotifyGiveFromISR(TaskHandle_t xTaskToNotify, BaseType_t *pxHigherPriorityTaskWoken)
{
    if (!is_valid_udram_addr(pxHigherPriorityTaskWoken) && pxHigherPriorityTaskWoken != NULL) {
        return;
    }

    int wrapper_index = (int)xTaskToNotify;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_TASK_ID);
    if (wrapper_handle == NULL) {
        return;
    }

    vTaskNotifyGiveFromISR((TaskHandle_t)wrapper_handle->handle, pxHigherPriorityTaskWoken);
}

uint32_t sys_ulTaskNotifyTake(BaseType_t xClearCountOnExit, TickType_t xTicksToWait)
{
    return ulTaskNotifyTake(xClearCountOnExit, xTicksToWait);
}

BaseType_t sys_xTaskNotifyWait(uint32_t ulBitsToClearOnEntry, uint32_t ulBitsToClearOnExit, uint32_t *pulNotificationValue, TickType_t xTicksToWait)
{
    if (!is_valid_udram_addr(pulNotificationValue)) {
        ESP_LOGE(TAG, "Invalid pulNotificationValue pointer");
        return pdFAIL;
    }
    return xTaskNotifyWait(ulBitsToClearOnEntry, ulBitsToClearOnExit, pulNotificationValue, xTicksToWait);
}

BaseType_t sys_xTaskNotifyStateClear(TaskHandle_t xTask)
{
    if (xTask == NULL) {
        return xTaskNotifyStateClear(NULL);
    }
    int wrapper_index = (int)xTask;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_TASK_ID);
    if (wrapper_handle == NULL) {
        return -1;
    }
    return xTaskNotifyStateClear((TaskHandle_t)wrapper_handle->handle);
}

TaskHandle_t sys_xTaskGetCurrentTaskHandle(void)
{
    TaskHandle_t handle = xTaskGetCurrentTaskHandle();
    return (TaskHandle_t)pvTaskGetThreadLocalStoragePointer(handle, ESP_PA_TLS_OFFSET_SHIM_HANDLE);
}

TaskHandle_t sys_xTaskGetIdleTaskHandle(void)
{
    return xTaskGetIdleTaskHandle();
}

UBaseType_t sys_uxTaskGetStackHighWaterMark(TaskHandle_t xTask)
{
#if (INCLUDE_uxTaskGetStackHighWaterMark == 1)
    /* uxTaskGetStackHighWaterMark is a system call in user space and we switch the stack to kernel stack
     * when servicing a system call. If we call `uxTaskGetStackHighWaterMark` here, it will give us the high watermark for
     * kernel space stack which is incorrect as the user expects the high watermark for the user space stack. So we
     * manually calculate the high watermark here
     */
    TaskHandle_t handle;
    if (xTask == NULL) {
        handle = xTaskGetCurrentTaskHandle();
    } else {
        int wrapper_index = (int)xTask;
        esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_TASK_ID);
        if (wrapper_handle == NULL) {
            return -1;
        }
        handle = (TaskHandle_t)wrapper_handle->handle;
    }

    uint32_t high_watermark = 0U;

    const uint8_t *stackstart = pxTaskGetStackStart(handle);

    if (is_valid_kdram_addr((void *)stackstart)) {
        /* High watermark can be queried for a task executing system-call, in that case, the stack will point to kernel stack.
         * Retrieve the user stack from system call stack frame
         */
        RvEcallFrame *syscall_stack = (RvEcallFrame *)((uint32_t)stackstart + KERNEL_STACK_SIZE - RV_ESTK_FRMSZ);
        stackstart = (const uint8_t *)syscall_stack->stack;
    }

    while (*stackstart == (uint8_t)tskSTACK_FILL_BYTE) {
        stackstart -= portSTACK_GROWTH;
        high_watermark++;
    }

    high_watermark /= (uint32_t)sizeof(StackType_t);

    return (configSTACK_DEPTH_TYPE)high_watermark;
#else
    return -1;
#endif
}

eTaskState sys_eTaskGetState(TaskHandle_t xTask)
{
    if (xTask == NULL) {
        return -1;
    }
    int wrapper_index = (int)xTask;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_TASK_ID);
    if (wrapper_handle == NULL) {
        return -1;
    }
    return eTaskGetState((TaskHandle_t)wrapper_handle->handle);
}

char *sys_pcTaskGetName(TaskHandle_t xTaskToQuery)
{
    if (xTaskToQuery == NULL) {
        return pcTaskGetName(NULL);
    }
    int wrapper_index = (int)xTaskToQuery;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_TASK_ID);
    if (wrapper_handle == NULL) {
        return NULL;
    }
    return pcTaskGetName((TaskHandle_t)wrapper_handle->handle);
}

TaskHandle_t sys_xTaskGetHandle(const char *pcNameToQuery)
{
#if INCLUDE_xTaskGetHandle
    if (is_valid_user_d_addr((void *)pcNameToQuery)) {
        TaskHandle_t handle = xTaskGetHandle(pcNameToQuery);
        return (TaskHandle_t)pvTaskGetThreadLocalStoragePointer(handle, ESP_PA_TLS_OFFSET_SHIM_HANDLE);
    } else {
        return NULL;
    }
#endif
    return 0;
}

TickType_t sys_xTaskGetTickCount(void)
{
    return xTaskGetTickCount();
}

TickType_t sys_xTaskGetTickCountFromISR(void)
{
    return xTaskGetTickCountFromISR();
}

BaseType_t sys_xTaskGetSchedulerState(void)
{
    return xTaskGetSchedulerState();
}

UBaseType_t sys_uxTaskGetNumberOfTasks(void)
{
    return uxTaskGetNumberOfTasks();
}

QueueHandle_t sys_xQueueGenericCreate(uint32_t QueueLength, uint32_t ItemSize, uint8_t ucQueueType)
{
    QueueHandle_t q;
    if (ucQueueType == queueQUEUE_TYPE_CLEANUP) {
        if (!usr_mem_cleanup_queue_index) {
            q = xQueueGenericCreate(QueueLength, ItemSize, queueQUEUE_TYPE_BASE);
            if (q == NULL) {
                return NULL;
            }
            usr_mem_cleanup_queue_index = (intptr_t)esp_map_add(q, ESP_MAP_QUEUE_ID);
            if (!usr_mem_cleanup_queue_index) {
                ESP_LOGE(TAG, "Insufficient memory for shim struct");
                vQueueDelete(q);
                return NULL;
            }
            usr_mem_cleanup_queue_handle = q;
        }
        return (QueueHandle_t)usr_mem_cleanup_queue_index;
    }

    if (ucQueueType == queueQUEUE_TYPE_DISPATCH) {
        if (!usr_dispatcher_queue_index) {
            q = xQueueGenericCreate(QueueLength, ItemSize, queueQUEUE_TYPE_BASE);
            if (q == NULL) {
                return NULL;
            }
            usr_dispatcher_queue_index = (intptr_t)esp_map_add(q, ESP_MAP_QUEUE_ID);
            if (!usr_dispatcher_queue_index) {
                ESP_LOGE(TAG, "Insufficient memory for shim struct");
                vQueueDelete(q);
                return NULL;
            }
            usr_dispatcher_queue_handle = q;
        }
        return (QueueHandle_t)usr_dispatcher_queue_index;
    }

    q = xQueueGenericCreate(QueueLength, ItemSize, ucQueueType);
    if (q == NULL) {
        return NULL;
    }

    int wrapper_index = esp_map_add(q, ESP_MAP_QUEUE_ID);
    if (!wrapper_index) {
        ESP_LOGE(TAG, "Insufficient memory for shim struct");
        vQueueDelete(q);
        return NULL;
    }
    return (QueueHandle_t)wrapper_index;
}

void sys_vQueueDelete(QueueHandle_t xQueue)
{
    if (xQueue == (QueueHandle_t)usr_mem_cleanup_queue_index) {
        ESP_LOGE(TAG, "User mem cleanup queue deletion forbidden");
        return;
    }

    if (xQueue == (QueueHandle_t)usr_dispatcher_queue_index) {
        ESP_LOGE(TAG, "User dispatcher queue deletion forbidden");
        return;
    }

    int wrapper_index = (int)xQueue;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_QUEUE_ID);
    if (wrapper_handle == NULL) {
        return;
    }
    vQueueDelete((QueueHandle_t)wrapper_handle->handle);
    esp_map_remove(wrapper_index);
}

BaseType_t sys_xQueueGenericSend(QueueHandle_t xQueue, const void * const pvItemToQueue, TickType_t xTicksToWait, const BaseType_t xCopyPosition)
{
    int wrapper_index = (int)xQueue;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_QUEUE_ID);
    if (wrapper_handle == NULL) {
        return 0;
    }
    // Incase of Semaphore, pvItemToQueue is NULL. Add an extra check for uxItemSize, which for Semaphore is 0
    // If the uxItemSize if 0, we can allow invalid pvItemToQueue pointer
    if ((is_valid_user_d_addr((void *)pvItemToQueue) || ((StaticQueue_t *)wrapper_handle->handle)->uxDummy4[2] == 0)) {
        return xQueueGenericSend((QueueHandle_t)wrapper_handle->handle, pvItemToQueue, xTicksToWait, xCopyPosition);
    } else {
        return 0;
    }
}

BaseType_t sys_xQueueGenericSendFromISR(QueueHandle_t xQueue, const void * const pvItemToQueue, BaseType_t * const pxHigherPriorityTaskWoken, const BaseType_t xCopyPosition)
{
    if (!is_valid_udram_addr((void *)pxHigherPriorityTaskWoken) && pxHigherPriorityTaskWoken != NULL) {
        return 0;
    }

    if (is_valid_user_d_addr((void *)pvItemToQueue)) {
        int wrapper_index = (int)xQueue;
        esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_QUEUE_ID);
        if (wrapper_handle == NULL) {
            return 0;
        }
        return xQueueGenericSendFromISR((QueueHandle_t)wrapper_handle->handle, pvItemToQueue, pxHigherPriorityTaskWoken, xCopyPosition);
    } else {
        return 0;
    }
}

int sys_xQueueReceive(QueueHandle_t xQueue, void * const buffer, TickType_t TickstoWait)
{
    if (is_valid_udram_addr(buffer)) {
        int wrapper_index = (int)xQueue;
        esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_QUEUE_ID);
        if (wrapper_handle == NULL) {
            return 0;
        }
        return xQueueReceive((QueueHandle_t)wrapper_handle->handle, buffer, TickstoWait);
    }
    return -1;
}

BaseType_t sys_xQueueReceiveFromISR(QueueHandle_t xQueue, void * const pvBuffer, BaseType_t * const pxHigherPriorityTaskWoken)
{
    if (!is_valid_udram_addr((void *)pxHigherPriorityTaskWoken) && pxHigherPriorityTaskWoken != NULL) {
        return 0;
    }

    if (is_valid_udram_addr((void *)pvBuffer)) {
        int wrapper_index = (int)xQueue;
        esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_QUEUE_ID);
        if (wrapper_handle == NULL) {
            return 0;
        }
        return xQueueReceiveFromISR((QueueHandle_t)wrapper_handle->handle, pvBuffer, pxHigherPriorityTaskWoken);
    } else {
        return 0;
    }
}

UBaseType_t sys_uxQueueMessagesWaiting(const QueueHandle_t xQueue)
{
    int wrapper_index = (int)xQueue;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_QUEUE_ID);
    if (wrapper_handle == NULL) {
        return 0;
    }
    return uxQueueMessagesWaiting((QueueHandle_t)wrapper_handle->handle);
}

UBaseType_t sys_uxQueueMessagesWaitingFromISR(const QueueHandle_t xQueue)
{
    int wrapper_index = (int)xQueue;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_QUEUE_ID);
    if (wrapper_handle == NULL) {
        return 0;
    }
    return uxQueueMessagesWaitingFromISR((QueueHandle_t)wrapper_handle->handle);
}

UBaseType_t sys_uxQueueSpacesAvailable(const QueueHandle_t xQueue)
{
    int wrapper_index = (int)xQueue;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_QUEUE_ID);
    if (wrapper_handle == NULL) {
        return 0;
    }
    return uxQueueSpacesAvailable((QueueHandle_t)wrapper_handle->handle);
}

BaseType_t sys_xQueueGenericReset(QueueHandle_t xQueue, BaseType_t xNewQueue)
{
    int wrapper_index = (int)xQueue;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_QUEUE_ID);
    if (wrapper_handle == NULL) {
        return 0;
    }
    return xQueueGenericReset((QueueHandle_t)wrapper_handle->handle, xNewQueue);
}

BaseType_t sys_xQueuePeek(QueueHandle_t xQueue, void * const pvBuffer, TickType_t xTicksToWait)
{
    if (is_valid_udram_addr((void *)pvBuffer)) {
        int wrapper_index = (int)xQueue;
        esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_QUEUE_ID);
        if (wrapper_handle == NULL) {
            return 0;
        }
        return xQueuePeek((QueueHandle_t)wrapper_handle->handle, pvBuffer, xTicksToWait);
    } else {
        return 0;
    }
}

BaseType_t sys_xQueuePeekFromISR(QueueHandle_t xQueue,  void * const pvBuffer)
{
    if (is_valid_udram_addr((void *)pvBuffer)) {
        int wrapper_index = (int)xQueue;
        esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_QUEUE_ID);
        if (wrapper_handle == NULL) {
            return 0;
        }
        return xQueuePeekFromISR((QueueHandle_t)wrapper_handle->handle, pvBuffer);
    } else {
        return 0;
    }
}

BaseType_t sys_xQueueIsQueueEmptyFromISR(const QueueHandle_t xQueue)
{
    int wrapper_index = (int)xQueue;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_QUEUE_ID);
    if (wrapper_handle == NULL) {
        return 0;
    }
    return xQueueIsQueueEmptyFromISR((QueueHandle_t)wrapper_handle->handle);
}

BaseType_t sys_xQueueIsQueueFullFromISR(const QueueHandle_t xQueue)
{
    int wrapper_index = (int)xQueue;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_QUEUE_ID);
    if (wrapper_handle == NULL) {
        return 0;
    }
    return xQueueIsQueueFullFromISR((QueueHandle_t)wrapper_handle->handle);
}

QueueSetHandle_t sys_xQueueCreateSet(const UBaseType_t uxEventQueueLength)
{
    QueueSetHandle_t pxQueue;

    pxQueue = sys_xQueueGenericCreate(uxEventQueueLength, sizeof(QueueHandle_t *), queueQUEUE_TYPE_SET);

    int wrapper_index = esp_map_add(pxQueue, ESP_MAP_QUEUE_ID);
    if (!wrapper_index) {
        vQueueDelete(pxQueue);
        return NULL;
    }

    return (QueueSetHandle_t)wrapper_index;
}

BaseType_t sys_xQueueAddToSet(QueueSetMemberHandle_t xQueueOrSemaphore, QueueSetHandle_t xQueueSet)
{
    int wrapper_index = (int)xQueueOrSemaphore;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_QUEUE_ID);
    if (wrapper_handle == NULL) {
        return 0;
    }
    return xQueueAddToSet(xQueueOrSemaphore, (QueueHandle_t)wrapper_handle->handle);
}

BaseType_t sys_xQueueRemoveFromSet(QueueSetMemberHandle_t xQueueOrSemaphore, QueueSetHandle_t xQueueSet)
{
    int wrapper_index = (int)xQueueOrSemaphore;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_QUEUE_ID);
    if (wrapper_handle == NULL) {
        return 0;
    }
    return xQueueRemoveFromSet(xQueueOrSemaphore, (QueueHandle_t)wrapper_handle->handle);
}

QueueSetMemberHandle_t sys_xQueueSelectFromSet(QueueSetHandle_t xQueueSet, TickType_t const xTicksToWait)
{
    int wrapper_index = (int)xQueueSet;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_QUEUE_ID);
    if (wrapper_handle == NULL) {
        return NULL;
    }
    return xQueueSelectFromSet((QueueHandle_t)wrapper_handle->handle, xTicksToWait);
}

QueueSetMemberHandle_t sys_xQueueSelectFromSetFromISR(QueueSetHandle_t xQueueSet)
{
    int wrapper_index = (int)xQueueSet;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_QUEUE_ID);
    if (wrapper_handle == NULL) {
        return NULL;
    }
    return xQueueSelectFromSetFromISR((QueueHandle_t)wrapper_handle->handle);
}

QueueHandle_t sys_xQueueCreateCountingSemaphore(const UBaseType_t uxMaxCount, const UBaseType_t uxInitialCount)
{
    QueueHandle_t q = xQueueCreateCountingSemaphore(uxMaxCount, uxInitialCount);
    if (q == NULL) {
        return NULL;
    }
    int wrapper_index = esp_map_add(q, ESP_MAP_QUEUE_ID);
    if (!wrapper_index) {
        vQueueDelete(q);
        return NULL;
    }
    return (QueueHandle_t)wrapper_index;
}

QueueHandle_t sys_xQueueCreateMutex(const uint8_t ucQueueType)
{
    QueueHandle_t q = xQueueCreateMutex(ucQueueType);
    if (q == NULL) {
        return NULL;
    }
    int wrapper_index = esp_map_add(q, ESP_MAP_QUEUE_ID);
    if (!wrapper_index) {
        vQueueDelete(q);
        return NULL;
    }
    return (QueueHandle_t)wrapper_index;
}

TaskHandle_t sys_xQueueGetMutexHolder(QueueHandle_t xSemaphore)
{
    int wrapper_index = (int)xSemaphore;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_QUEUE_ID);
    if (wrapper_handle == NULL) {
        return NULL;
    }
    TaskHandle_t xTask = xQueueGetMutexHolder((QueueHandle_t)wrapper_handle->handle);
    return (TaskHandle_t)pvTaskGetThreadLocalStoragePointer(xTask, ESP_PA_TLS_OFFSET_SHIM_HANDLE);
}

BaseType_t sys_xQueueSemaphoreTake(QueueHandle_t xQueue, TickType_t xTicksToWait)
{
    int wrapper_index = (int)xQueue;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_QUEUE_ID);
    if (wrapper_handle == NULL) {
        return 0;
    }
    // Incase of Semaphore, pvItemToQueue is NULL. Add an extra check for uxItemSize, which for Semaphore is 0.
    if (((StaticQueue_t *)wrapper_handle->handle)->uxDummy4[2] != 0) {
        return 0;
    }
    return xQueueSemaphoreTake((QueueHandle_t)wrapper_handle->handle, xTicksToWait);
}

BaseType_t sys_xQueueTakeMutexRecursive(QueueHandle_t xMutex, TickType_t xTicksToWait)
{
    int wrapper_index = (int)xMutex;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_QUEUE_ID);
    if (wrapper_handle == NULL) {
        return 0;
    }
    return xQueueTakeMutexRecursive((QueueHandle_t)wrapper_handle->handle, xTicksToWait);
}

BaseType_t sys_xQueueGiveMutexRecursive(QueueHandle_t xMutex)
{
    int wrapper_index = (int)xMutex;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_QUEUE_ID);
    if (wrapper_handle == NULL) {
        return 0;
    }
    return xQueueGiveMutexRecursive((QueueHandle_t)wrapper_handle->handle);
}

BaseType_t sys_xQueueGiveFromISR(QueueHandle_t xQueue, BaseType_t * const pxHigherPriorityTaskWoken)
{
    if (!is_valid_udram_addr((void *)pxHigherPriorityTaskWoken) && pxHigherPriorityTaskWoken != NULL) {
        return pdFAIL;
    }

    int wrapper_index = (int)xQueue;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_QUEUE_ID);
    if (wrapper_handle == NULL) {
        return 0;
    }
    return xQueueGiveFromISR((QueueHandle_t)wrapper_handle->handle, pxHigherPriorityTaskWoken);
}

void sys_xtimer_cb(void *timer)
{
    if (!usr_dispatcher_queue_handle) {
        return;
    }

    usr_xtimer_context_t* usr_context = (usr_xtimer_context_t*)pvTimerGetTimerID(timer);
    usr_dispatch_ctx_t dispatch_ctx = {
        .event = ESP_SYSCALL_EVENT_XTIMER,
    };
    memcpy(&dispatch_ctx.dispatch_data.xtimer_args, usr_context, sizeof(usr_xtimer_context_t));

    if (xQueueSend(usr_dispatcher_queue_handle, &dispatch_ctx, portMAX_DELAY) != pdPASS) {
        return;
    }
}

TimerHandle_t sys_xTimerCreate(const char * const pcTimerName,
                                      const TickType_t xTimerPeriodInTicks,
                                      const UBaseType_t uxAutoReload,
                                      void * const pvTimerID,
                                      TimerCallbackFunction_t pxCallbackFunction)
{
    TimerHandle_t handle;
    int wrapper_index = 0;

    /* pcTimerName is never used by FreeRTOS kernel. It is just for debugging purpose by the application.
     * We have system call for `pcTimerGetName` so we cannot replace the timer name with our name
     */

    if (!(is_valid_user_i_addr(pxCallbackFunction))) {
        ESP_LOGE(TAG, "Incorrect address space for callback function or user queue");
        return NULL;
    }

    usr_xtimer_context_t *usr_context = heap_caps_malloc(sizeof(usr_xtimer_context_t), MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL);
    if (!usr_context) {
        ESP_LOGE(TAG, "Insufficient memory for user context");
        return NULL;
    }

    handle = xTimerCreate(pcTimerName, xTimerPeriodInTicks, uxAutoReload, usr_context, sys_xtimer_cb);

    if (handle == NULL) {
        free(usr_context);
    } else {
        wrapper_index = esp_map_add(handle, ESP_MAP_XTIMER_ID);
        if (!wrapper_index) {
            xTimerDelete(handle, portMAX_DELAY);
            free(usr_context);
            return NULL;
        }
        usr_context->usr_cb = pxCallbackFunction;
        usr_context->usr_timer_id = pvTimerID;
        usr_context->timerhandle = (void *)wrapper_index;
    }
    return (TimerHandle_t)wrapper_index;
}

BaseType_t sys_xTimerIsTimerActive(TimerHandle_t xTimer)
{
    int wrapper_index = (int)xTimer;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_XTIMER_ID);
    if (wrapper_handle == NULL) {
        return pdFALSE;
    }
    return xTimerIsTimerActive((TimerHandle_t)wrapper_handle->handle);
}

void *sys_pvTimerGetTimerID(const TimerHandle_t xTimer)
{
    int wrapper_index = (int)xTimer;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_XTIMER_ID);
    if (wrapper_handle == NULL) {
        return NULL;
    }
    usr_xtimer_context_t *usr_context = (usr_xtimer_context_t*)pvTimerGetTimerID((TimerHandle_t)wrapper_handle->handle);

    return usr_context->usr_timer_id;
}

const char *sys_pcTimerGetName(TimerHandle_t xTimer)
{
    int wrapper_index = (int)xTimer;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_XTIMER_ID);
    if (wrapper_handle == NULL) {
        return NULL;
    }
    return pcTimerGetName((TimerHandle_t)wrapper_handle->handle);
}

void sys_vTimerSetReloadMode(TimerHandle_t xTimer, const UBaseType_t uxAutoReload)
{
    int wrapper_index = (int)xTimer;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_XTIMER_ID);
    if (wrapper_handle == NULL) {
        return;
    }
    vTimerSetReloadMode((TimerHandle_t)wrapper_handle->handle, uxAutoReload);
}

BaseType_t sys_xTimerGenericCommand(TimerHandle_t xTimer, const BaseType_t xCommandID, const TickType_t xOptionalValue, BaseType_t * const pxHigherPriorityTaskWoken, const TickType_t xTicksToWait)
{
    if (!is_valid_udram_addr((void *)pxHigherPriorityTaskWoken) && pxHigherPriorityTaskWoken != NULL) {
        return pdFAIL;
    }

    int wrapper_index = (int)xTimer;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_XTIMER_ID);
    if (wrapper_handle == NULL) {
        return pdFAIL;
    }
    int ret = xTimerGenericCommand((TimerHandle_t)wrapper_handle->handle, xCommandID, xOptionalValue, pxHigherPriorityTaskWoken, xTicksToWait);
    if (ret == pdPASS && xCommandID == tmrCOMMAND_DELETE) {
        esp_map_remove(wrapper_index);
    }
    return ret;
}

void sys_vTimerSetTimerID(TimerHandle_t xTimer, void *pvNewID)
{
    int wrapper_index = (int)xTimer;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_XTIMER_ID);
    if (wrapper_handle == NULL) {
        return;
    }
    usr_xtimer_context_t *usr_context = (usr_xtimer_context_t*)pvTimerGetTimerID((TimerHandle_t)wrapper_handle->handle);

    usr_context->usr_timer_id = pvNewID;
}

TickType_t sys_xTimerGetPeriod(TimerHandle_t xTimer)
{
    int wrapper_index = (int)xTimer;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_XTIMER_ID);
    if (wrapper_handle == NULL) {
        return 0;
    }
    return xTimerGetPeriod((TimerHandle_t)wrapper_handle->handle);
}

TickType_t sys_xTimerGetExpiryTime(TimerHandle_t xTimer)
{
    int wrapper_index = (int)xTimer;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_XTIMER_ID);
    if (wrapper_handle == NULL) {
        return 0;
    }
    return xTimerGetExpiryTime((TimerHandle_t)wrapper_handle->handle);
}

EventGroupHandle_t sys_xEventGroupCreate(void)
{
    EventGroupHandle_t handle = xEventGroupCreate();
    if (handle == NULL) {
        return NULL;
    }
    int wrapper_index = esp_map_add(handle, ESP_MAP_EVENT_GROUP_ID);
    if (!wrapper_index) {
        vEventGroupDelete(handle);
        return NULL;
    }
    return (EventGroupHandle_t)wrapper_index;
}

EventBits_t sys_xEventGroupWaitBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToWaitFor, const BaseType_t xClearOnExit, const BaseType_t xWaitForAllBits, TickType_t xTicksToWait)
{
    int wrapper_index = (int)xEventGroup;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_EVENT_GROUP_ID);
    if (wrapper_handle == NULL) {
        return 0;
    }
    return xEventGroupWaitBits((EventGroupHandle_t)wrapper_handle->handle, uxBitsToWaitFor, xClearOnExit, xWaitForAllBits, xTicksToWait);
}

EventBits_t sys_xEventGroupSetBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet)
{
    int wrapper_index = (int)xEventGroup;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_EVENT_GROUP_ID);
    if (wrapper_handle == NULL) {
        return 0;
    }
    return xEventGroupSetBits((EventGroupHandle_t)wrapper_handle->handle, uxBitsToSet);
}

EventBits_t sys_xEventGroupClearBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToClear)
{
    int wrapper_index = (int)xEventGroup;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_EVENT_GROUP_ID);
    if (wrapper_handle == NULL) {
        return 0;
    }
    return xEventGroupClearBits((EventGroupHandle_t)wrapper_handle->handle, uxBitsToClear);
}

EventBits_t sys_xEventGroupSync(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet, const EventBits_t uxBitsToWaitFor, TickType_t xTicksToWait)
{
    int wrapper_index = (int)xEventGroup;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_EVENT_GROUP_ID);
    if (wrapper_handle == NULL) {
        return 0;
    }
    return xEventGroupSync((EventGroupHandle_t)wrapper_handle->handle, uxBitsToSet, uxBitsToWaitFor, xTicksToWait);
}

void sys_vEventGroupDelete(EventGroupHandle_t xEventGroup)
{
    int wrapper_index = (int)xEventGroup;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_EVENT_GROUP_ID);
    if (wrapper_handle == NULL) {
        return;
    }
    vEventGroupDelete((EventGroupHandle_t)wrapper_handle->handle);
    esp_map_remove(wrapper_index);
    return;
}

int sys_lwip_socket(int domain, int type, int protocol)
{
    return lwip_socket(domain, type, protocol);
}

int sys_lwip_connect(int s, const struct sockaddr *name, socklen_t namelen)
{
    if (!is_valid_udram_addr((void *)name)) {
        return -1;
    }
    return lwip_connect(s, name, namelen);
}

int sys_lwip_accept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
    if (!is_valid_udram_addr(addr) || !is_valid_udram_addr(addrlen)) {
        return -1;
    }
    return lwip_accept(s, addr, addrlen);
}

int sys_lwip_bind(int s, const struct sockaddr *name, socklen_t namelen)
{
    if (!is_valid_udram_addr((void *)name)) {
        return -1;
    }
    return lwip_bind(s, name, namelen);
}

int sys_lwip_shutdown(int s, int how)
{
    return lwip_shutdown(s, how);
}

int sys_getaddrinfo(const char *nodename, const char *servname,
       const struct addrinfo *hints, struct addrinfo **res)
{
    int ret;
    struct addrinfo *tmp_res;
    ret = getaddrinfo(nodename, servname, hints, &tmp_res);
    if (ret == 0) {
        struct addrinfo *usr_res = *res;
        if (!is_valid_udram_addr(usr_res) || !is_valid_udram_addr(usr_res + NETDB_ELEM_SIZE)) {
            ESP_LOGE(TAG, "Invalid user addrinfo pointer");
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

        freeaddrinfo(tmp_res);
    }
    return ret;
}

int sys_lwip_getpeername(int s, struct sockaddr *name, socklen_t *namelen)
{
    if (!is_valid_udram_addr((void *)name) || !is_valid_udram_addr((void *)namelen)) {
        return -1;
    }

    return lwip_getpeername(s, name, namelen);
}

int sys_lwip_getsockname(int s, struct sockaddr *name, socklen_t *namelen)
{
    if (!is_valid_udram_addr((void *)name) || !is_valid_udram_addr((void *)namelen)) {
        return -1;
    }

    return lwip_getsockname(s, name, namelen);
}

int sys_lwip_setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen)
{
    if (!is_valid_udram_addr((void *)optval) || !is_valid_udram_addr((void *)((int)optval + optlen))) {
        return -1;
    }
    return lwip_setsockopt(s, level, optname, optval, optlen);
}

int sys_lwip_getsockopt(int s, int level, int optname, void *optval, socklen_t *optlen)
{
    if (!is_valid_udram_addr((void *)optval) || !is_valid_udram_addr((void *)optlen)) {
        return -1;
    }

    return lwip_getsockopt(s, level, optname, optval, optlen);
}

int sys_lwip_listen(int s, int backlog)
{
    return lwip_listen(s, backlog);
}

size_t sys_lwip_recv(int s, void *mem, size_t len, int flags)
{
    if (!is_valid_udram_addr(mem) || !is_valid_udram_addr((void *)((int)mem + len))) {
        return -1;
    }
    return lwip_recv(s, mem, len, flags);
}

ssize_t sys_lwip_recvmsg(int s, struct msghdr *message, int flags)
{
    if (!is_valid_udram_addr((void *)message) ||
        !is_valid_udram_addr((void *)message->msg_iov) ||
        !is_valid_udram_addr((void *)message->msg_iov->iov_base) ||
        !is_valid_udram_addr((void *)message->msg_control)) {
        return -1;
    }

    return lwip_recvmsg(s, message, flags);
}

ssize_t sys_lwip_recvfrom(int s, void *mem, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen)
{
    if (!is_valid_udram_addr((void *)mem) || !is_valid_udram_addr((void *)((int)mem + len)) ||
        !is_valid_udram_addr((void *)from) ||
        !is_valid_udram_addr((void *)fromlen)) {
        return -1;
    }

    return lwip_recvfrom(s, mem, len, flags, from, fromlen);
}

size_t sys_lwip_send(int s, const void *data, size_t size, int flags)
{
    if (!is_valid_user_d_addr((void *)data) || !is_valid_user_d_addr((void *)((int)data + size))) {
        return -1;
    }
    return lwip_send(s, data, size, flags);
}

ssize_t sys_lwip_sendmsg(int s, const struct msghdr *msg, int flags)
{
    if (!is_valid_udram_addr((void *)msg) ||
        !is_valid_udram_addr((void *)msg->msg_iov) ||
        !is_valid_udram_addr((void *)msg->msg_iov->iov_base) ||
        !is_valid_udram_addr((void *)msg->msg_control)) {
        return -1;
    }

    return lwip_sendmsg(s, msg, flags);
}

ssize_t sys_lwip_sendto(int s, const void *data, size_t size, int flags, const struct sockaddr *to, socklen_t tolen)
{
    if (!is_valid_user_d_addr((void *)data) || !is_valid_user_d_addr((void *)((int)data + size)) ||
        !is_valid_user_d_addr((void *)to)) {
        return -1;
    }

    return lwip_sendto(s, data, size, flags, to, tolen);
}

const char *sys_lwip_inet_ntop(int af, const void *src, char *dst, socklen_t size)
{
    if (!is_valid_user_d_addr((void *)src) ||
        !is_valid_udram_addr((void *)dst)) {
        return NULL;
    }

    return lwip_inet_ntop(af, src, dst, size);
}

int sys_lwip_inet_pton(int af, const char *src, void *dst)
{
    if (!is_valid_user_d_addr((void *)src) ||
        !is_valid_udram_addr((void *)dst)) {
        return -1;
    }

    return lwip_inet_pton(af, src, dst);
}

u32_t sys_lwip_htonl(u32_t n)
{
    return lwip_htonl(n);
}

u16_t sys_lwip_htons(u16_t n)
{
    return lwip_htons(n);
}

int *sys___errno(void)
{
    return pvTaskGetThreadLocalStoragePointer(NULL, ESP_PA_TLS_OFFSET_ERRNO);
}

/* __wrap___errno is called when errno is accessed in protected app */
int *__wrap___errno(void)
{
    void *tls_errno = pvTaskGetThreadLocalStoragePointer(NULL, ESP_PA_TLS_OFFSET_ERRNO);
    // If TLS pointer is set, it indicates a user task.
    if (tls_errno) {
        return tls_errno;
    }
    return __real___errno();
}

int sys_open(const char *path, int flags, int mode)
{
    if (is_valid_user_d_addr((void *)path)) {
        return open(path, flags, mode);
    }
    return -1;
}

int sys_write(int s, const void *data, size_t size)
{
    if (is_valid_user_d_addr((void *)data) && is_valid_user_d_addr((void *)((int)data + size))) {
        return write(s, data, size);
    }
    return -1;
}

int sys_read(int s, void *mem, size_t len)
{
    if (is_valid_udram_addr(mem) && is_valid_udram_addr((void *)((int)mem + len))) {
        return read(s, mem, len);
    }
    return -1;
}

int sys_close(int s)
{
    return close(s);
}

int sys_select(int maxfdp1, fd_set *readset, fd_set *writeset, fd_set *exceptset, struct timeval *timeout)
{
    if (is_valid_kernel_d_addr((void *)readset) ||
        is_valid_kernel_d_addr((void *)writeset) ||
        is_valid_kernel_d_addr((void *)exceptset)) {
        return -1;
    }

    return select(maxfdp1, readset, writeset, exceptset, timeout);
}

int sys_poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    if (!is_valid_udram_addr((void *)fds)) {
        return -1;
    }

    return poll(fds, nfds, timeout);
}

int sys_ioctl(int s, long cmd, void *argp)
{
    if (!is_valid_user_d_addr((void *)argp)) {
        return -1;
    }

    return ioctl(s, cmd, argp);
}

int sys_fcntl(int s, int cmd, int val)
{
    return fcntl(s, cmd, val);
}

IRAM_ATTR void sys_gpio_isr_handler(void *args)
{
    if (!usr_dispatcher_queue_handle) {
        return;
    }

    int need_yield;
    usr_dispatch_ctx_t dispatch_ctx = {
        .event = ESP_SYSCALL_EVENT_GPIO,
    };
    memcpy(&dispatch_ctx.dispatch_data.gpio_args, args, sizeof(usr_gpio_args_t));
    if (xQueueSendFromISR(usr_dispatcher_queue_handle, &dispatch_ctx, &need_yield) != pdPASS) {
        return;

    }
    if (need_yield == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

esp_err_t sys_gpio_config(const gpio_config_t *conf)
{
    if (is_valid_user_d_addr((void *)conf)) {
        return gpio_config(conf);
    }

    return ESP_ERR_INVALID_ARG;
}

esp_err_t sys_gpio_install_isr_service(int intr_alloc_flags)
{
    return gpio_install_isr_service(intr_alloc_flags);
}

esp_err_t sys_gpio_softisr_handler_add(gpio_num_t gpio_num, gpio_isr_t isr_handler, void *args, usr_gpio_handle_t *gpio_handle)
{
    esp_err_t ret;
    if (!(is_valid_udram_addr(gpio_handle))) {
        ESP_LOGE(TAG, "Incorrect address space for gpio handle or user queue");
        return ESP_ERR_INVALID_ARG;
    }

    usr_gpio_args_t *usr_context = heap_caps_malloc(sizeof(usr_gpio_args_t), MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL);
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
        int wrapper_index = esp_map_add(usr_context, ESP_MAP_GPIO_ID);
        *gpio_handle = (usr_gpio_handle_t)wrapper_index;
    }
    return ret;
}

esp_err_t sys_gpio_softisr_handler_remove(usr_gpio_handle_t gpio_handle)
{
    esp_err_t ret;
    int wrapper_index = (int)gpio_handle;
    esp_map_handle_t *wrapper_handle = (esp_map_handle_t *)esp_map_verify(wrapper_index, ESP_MAP_GPIO_ID);
    if (!wrapper_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    usr_gpio_args_t *usr_context = (usr_gpio_args_t *)wrapper_handle->handle;
    ret = gpio_isr_handler_remove(usr_context->gpio_num);
    if (ret == ESP_OK) {
        gpio_uninstall_isr_service();
        free(usr_context);
        esp_map_remove(wrapper_index);
    }
    return ret;
}

esp_err_t sys_gpio_set_level(gpio_num_t gpio_num, uint32_t level)
{
    return gpio_set_level(gpio_num, level);
}

esp_err_t sys_nvs_flash_init()
{
    return nvs_flash_init();
}

esp_err_t sys_esp_netif_init()
{
    return esp_netif_init();
}

esp_err_t sys_esp_event_loop_create_default()
{
    return esp_event_loop_create_default();
}

esp_netif_t* sys_esp_netif_create_default_wifi_sta()
{
    esp_netif_t *handle = esp_netif_create_default_wifi_sta();
    int wrapper_index = esp_map_add(handle, ESP_MAP_ESP_NETIF_ID);
    if (!wrapper_index) {
        esp_netif_destroy(handle);
        return NULL;
    }
    return (esp_netif_t *)wrapper_index;
}

void sys_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (!usr_dispatcher_queue_handle) {
        return;
    }

    usr_dispatch_ctx_t dispatch_ctx = {
        .event = ESP_SYSCALL_EVENT_ESP_EVENT,
    };

    if (event_base == WIFI_EVENT) {
        dispatch_ctx.dispatch_data.event_args.event_base = esp_event_map_arr[WIFI_EVENT_INDEX];
    } else if (event_base == IP_EVENT) {
        dispatch_ctx.dispatch_data.event_args.event_base = esp_event_map_arr[IP_EVENT_INDEX];
    } else {
        dispatch_ctx.dispatch_data.event_args.event_base = event_base;
    }

    dispatch_ctx.dispatch_data.event_args.event_id = event_id;
    dispatch_ctx.dispatch_data.event_args.event_data = event_data;
    memcpy(&dispatch_ctx.dispatch_data.event_args.usr_context, arg, sizeof(usr_context_t));

    if (xQueueSend(usr_dispatcher_queue_handle, &dispatch_ctx, portMAX_DELAY) == pdFALSE) {
        printf("Error sending message on queue\n");
    }
    return;
}

esp_err_t sys_esp_event_handler_instance_register(esp_event_base_t event_base,
                                              int32_t event_id,
                                              esp_event_handler_t event_handler,
                                              void *event_handler_arg,
                                              esp_event_handler_instance_t *context)
{
    esp_err_t ret;
    esp_event_base_t sys_event_base;

    /* Check the end of event_base string before actually comparing the string with
     * pre-registered events
     */
    if (!is_valid_user_d_addr((void *)event_base) ||
        !is_valid_user_d_addr((void *)((int)event_base + strlen(WIFI_EVENT))) ||
        !is_valid_user_d_addr((void *)((int)event_base + strlen(IP_EVENT)))) {
        ESP_LOGE(TAG, "Invalid event base");
        return ESP_ERR_INVALID_ARG;
    }

    if (strncmp(WIFI_EVENT, event_base, strlen(WIFI_EVENT)) == 0) {
        sys_event_base = WIFI_EVENT;
        esp_event_map_arr[WIFI_EVENT_INDEX] = event_base;
    } else if (strncmp(IP_EVENT, event_base, strlen(IP_EVENT)) == 0) {
        sys_event_base = IP_EVENT;
        esp_event_map_arr[IP_EVENT_INDEX] = event_base;
    } else {
        sys_event_base = event_base;
    }

    if (!(is_valid_udram_addr(context))) {
        ESP_LOGE(TAG, "Incorrect address space for context or user_queue");
        return ESP_ERR_INVALID_ARG;
    }

    usr_context_t *usr_context = heap_caps_malloc(sizeof(usr_context_t), MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL);
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

esp_err_t sys_esp_event_handler_instance_unregister(esp_event_base_t event_base,
                                                int32_t event_id,
                                                esp_event_handler_instance_t context)
{
    esp_err_t ret;
    esp_event_base_t sys_event_base;

    /* Check the end of event_base string before actually comparing the string with
     * pre-registered events
     */
    if (!is_valid_user_d_addr((void *)event_base) ||
        !is_valid_user_d_addr((void *)((int)event_base + strlen(WIFI_EVENT))) ||
        !is_valid_user_d_addr((void *)((int)event_base + strlen(IP_EVENT)))) {
        ESP_LOGE(TAG, "Invalid event base");
        return ESP_ERR_INVALID_ARG;
    }
    if (strncmp(WIFI_EVENT, event_base, strlen(WIFI_EVENT)) == 0) {
        sys_event_base = WIFI_EVENT;
    } else if (strncmp(IP_EVENT, event_base, strlen(IP_EVENT)) == 0) {
        sys_event_base = IP_EVENT;
    } else {
        sys_event_base = event_base;
    }

    usr_context_t *usr_context = (usr_context_t *)context;
    if (usr_context && is_valid_udram_addr(usr_context)) {
        ret = esp_event_handler_instance_unregister(sys_event_base, event_id, usr_context->event_handler_instance);
        if (ret == ESP_OK) {
            free(usr_context);
        }
        return ret;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t sys_esp_wifi_init(const wifi_init_config_t *config)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    return esp_wifi_init(&cfg);
}

esp_err_t sys_esp_wifi_set_mode(wifi_mode_t mode)
{
    return esp_wifi_set_mode(mode);
}

esp_err_t sys_esp_wifi_set_config(wifi_interface_t interface, wifi_config_t *conf)
{
    if (is_valid_udram_addr(conf)) {
        return esp_wifi_set_config(interface, conf);
    }
    return -1;
}

esp_err_t sys_esp_wifi_start()
{
    return esp_wifi_start();
}

esp_err_t sys_esp_wifi_connect()
{
    return esp_wifi_connect();
}

IRAM_ATTR uint32_t sys_esp_random(void)
{
    return esp_random();
}

void sys_esp_fill_random(void *buf, size_t len)
{
    if (!is_valid_udram_addr(buf) || !is_valid_udram_addr((void *)((int)buf + len))) {
        return;
    }
    esp_fill_random(buf, len);
}

void sys_esp_timer_dispatch_cb(void* arg)
{
    if (!usr_dispatcher_queue_handle) {
        return;
    }

    usr_dispatch_ctx_t dispatch_ctx = {
        .event = ESP_SYSCALL_EVENT_ESP_TIMER,
    };
    memcpy(&dispatch_ctx.dispatch_data.esp_timer_args, arg, sizeof(esp_timer_create_args_t));
    if (xQueueSend(usr_dispatcher_queue_handle, &dispatch_ctx, portMAX_DELAY) == pdFALSE) {
        ESP_LOGE(TAG, "Error sending message on queue");
    }
}

esp_err_t sys_esp_timer_create(const esp_timer_create_args_t* create_args,
        esp_timer_handle_t* out_handle)
{
    if (!is_valid_user_d_addr((void *)create_args) ||
            !is_valid_udram_addr(out_handle)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_timer_create_args_t *usr_args = (esp_timer_create_args_t *)heap_caps_malloc(sizeof(esp_timer_create_args_t), MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL);
    if (!usr_args) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(usr_args, create_args, sizeof(esp_timer_create_args_t));
    const esp_timer_create_args_t sys_create_args = {
        .callback = sys_esp_timer_dispatch_cb,
        .arg = usr_args,
        /* The `name` field is only used in esp_timer_dump and this function can only be called from
         * protected application so we can safely replace the user provide timer name with our own
         * name to ensure that no illegal access happens when accessing this string
         *
         * Revisit this when the strings pointers are correctly handled: US-83
         */
        .name = USER_TIMER_STRING,
        .skip_unhandled_events = create_args->skip_unhandled_events,
    };
    esp_timer_handle_t sys_timer_handle = NULL;
    esp_err_t err = esp_timer_create(&sys_create_args, &sys_timer_handle);
    if (err != ESP_OK) {
        free(usr_args);
    }
    esp_timer_handle_t wrapper_index = (esp_timer_handle_t)esp_map_add(sys_timer_handle, ESP_MAP_ESP_TIMER_ID);
    if (!wrapper_index) {
        esp_timer_delete(sys_timer_handle);
        free(usr_args);
        return ESP_ERR_NO_MEM;
    }
    *out_handle = wrapper_index;
    return err;
}

esp_err_t sys_esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us)
{
    int wrapper_index = (int)timer;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_ESP_TIMER_ID);
    if (wrapper_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_timer_start_once((esp_timer_handle_t)wrapper_handle->handle, timeout_us);
}

esp_err_t sys_esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period)
{
    int wrapper_index = (int)timer;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_ESP_TIMER_ID);
    if (wrapper_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_timer_start_periodic((esp_timer_handle_t)wrapper_handle->handle, period);
}

esp_err_t sys_esp_timer_stop(esp_timer_handle_t timer)
{
    int wrapper_index = (int)timer;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_ESP_TIMER_ID);
    if (wrapper_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_timer_stop((esp_timer_handle_t)wrapper_handle->handle);
}

esp_err_t sys_esp_timer_delete(esp_timer_handle_t timer)
{
    int wrapper_index = (int)timer;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_ESP_TIMER_ID);
    if (wrapper_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    usr_esp_timer_handle_t *handle = (usr_esp_timer_handle_t *)wrapper_handle->handle;
    void *arg = handle->arg;
    esp_err_t err = esp_timer_delete((esp_timer_handle_t)wrapper_handle->handle);
    if (err == ESP_OK) {
        free(arg);
        esp_map_remove(wrapper_index);
    }
    return err;
}

IRAM_ATTR int64_t sys_esp_timer_get_time(void)
{
    return esp_timer_get_time();
}

IRAM_ATTR int64_t sys_esp_timer_get_next_alarm(void)
{
    return esp_timer_get_next_alarm();
}

bool sys_esp_timer_is_active(esp_timer_handle_t timer)
{
    int wrapper_index = (int)timer;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_ESP_TIMER_ID);
    if (wrapper_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_timer_is_active((esp_timer_handle_t)wrapper_handle->handle);
}

int64_t IRAM_ATTR sys_esp_system_get_time(void)
{
    return esp_system_get_time();
}

esp_err_t sys_uart_driver_install(uart_port_t uart_num, int rx_buffer_size, int tx_buffer_size, int queue_size, QueueHandle_t* uart_queue, int intr_alloc_flags)
{
    if (!is_valid_udram_addr((void *)uart_queue)) {
        return ESP_ERR_INVALID_ARG;
    }
    QueueHandle_t sys_uart_queue = NULL;
    esp_err_t ret = uart_driver_install(uart_num, rx_buffer_size, tx_buffer_size, queue_size, &sys_uart_queue, intr_alloc_flags);
    if (ret != ESP_OK) {
        return ret;
    }
    uart_driver_queue_index[uart_num] = esp_map_add(sys_uart_queue, ESP_MAP_QUEUE_ID);
    *uart_queue = (QueueHandle_t)uart_driver_queue_index[uart_num];
    return ESP_OK;
}

esp_err_t sys_uart_driver_delete(uart_port_t uart_num)
{
    esp_err_t ret = uart_driver_delete(uart_num);
    if (ret == ESP_OK) {
        esp_map_remove(uart_driver_queue_index[uart_num]);
    }
    return ret;
}

int sys_uart_write_bytes(uart_port_t uart_num, const void* src, size_t size)
{
    if (!is_valid_user_d_addr((void *)src) || !is_valid_user_d_addr((void *)((int)src + size))) {
        return -1;
    }
    return uart_write_bytes(uart_num, src, size);
}

int sys_uart_read_bytes(uart_port_t uart_num, void* buf, uint32_t length, TickType_t ticks_to_wait)
{
    if (!is_valid_udram_addr(buf) || !is_valid_udram_addr((void *)((int)buf + length))) {
        return -1;
    }
    return uart_read_bytes(uart_num, buf, length, ticks_to_wait);
}

esp_err_t sys_esp_get_protected_heap_stats(protected_heap_stats_t *stats)
{
    if (!is_valid_udram_addr(stats)) {
        return ESP_ERR_INVALID_ARG;
    }
    stats->free_heap_size = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    stats->largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    stats->min_free_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    return ESP_OK;
}

IRAM_ATTR esp_err_t esp_syscall_spawn_user_task(void *user_entry, int stack_sz, usr_custom_app_desc_t *app_desc)
{
    usr_task_ctx_t task_ctx = {};
    usr_resources_t *user_resources = app_desc->user_app_resources;

    ESP_LOGI(TAG, "User entry point: %p", user_entry);

    task_ctx.stack = (StackType_t *)&user_resources->startup_stack;
    task_ctx.stack_size = stack_sz;
    task_ctx.task_errno = (int *)&user_resources->startup_errno;
    int ret = sys_xTaskCreate(user_entry, "User main task", NULL, 5, 0, &task_ctx);
    if (ret == pdPASS) {
        return ESP_OK;
    }
    return ESP_FAIL;
}

void esp_syscall_clear_user_resourses(void)
{
    int user_handles = esp_map_get_allocated_size();
    usr_dispatcher_queue_index = 0;
    usr_dispatcher_queue_handle = NULL;
    usr_mem_cleanup_queue_index = 0;
    usr_mem_cleanup_queue_handle = NULL;
    ESP_LOGI(TAG, "Deleting user_app resources");
    for (int i = ESP_MAP_INDEX_OFFSET; i < (ESP_MAP_INDEX_OFFSET + user_handles); i++) {
        esp_map_handle_t *wrapper_handle = esp_map_get_handle(i);
        if (!wrapper_handle) {
            continue;
        }
        switch (ESP_MAP_GET_ID(wrapper_handle)) {
            case ESP_MAP_QUEUE_ID:
                vQueueDelete((QueueHandle_t)wrapper_handle->handle);
                esp_map_remove(i);
                break;

            case ESP_MAP_TASK_ID:
                vTaskDelete((TaskHandle_t)wrapper_handle->handle);
                break;

            case ESP_MAP_ESP_TIMER_ID:
                esp_timer_stop((esp_timer_handle_t)wrapper_handle->handle);
                esp_timer_delete((esp_timer_handle_t)wrapper_handle->handle);
                esp_map_remove(i);
                break;

            case ESP_MAP_XTIMER_ID:
                xTimerDelete((TimerHandle_t)wrapper_handle->handle, 0);
                esp_map_remove(i);
                break;

            case ESP_MAP_EVENT_GROUP_ID:
                vEventGroupDelete((EventGroupHandle_t)wrapper_handle->handle);
                esp_map_remove(i);
                break;

            case ESP_MAP_GPIO_ID:
                sys_gpio_softisr_handler_remove((usr_gpio_handle_t)i);
                break;

            default:
                ESP_EARLY_LOGW(TAG, "Map ID 0x%x not checked", ESP_MAP_GET_ID(wrapper_handle));
                break;
        }
    }
}

#ifdef CONFIG_ESP_SYSCALL_VERIFY_RETURNED_POINTERS
IRAM_ATTR void sys_verify_returned_ptr(void *ptr, int syscall_num)
{
    if (is_valid_kdram_addr(ptr)) {
        ESP_EARLY_LOGE(TAG, "Raw kernel pointer returned by syscall %d. Aborting...", syscall_num);
        abort();
    }
}
#endif

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
