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

/* This header file only consists of functions that have different prototype from their IDF
 * equivalent functions.
 * The APIs marked `custom` in syscall.tbl must be declared here to allow compilation
 *
 * APIs that are marked `common` have the same prototype and the translation is done
 * in the build system.
 */
#include "soc_defs.h"
#include "hal/gpio_types.h"
#include "driver/gpio.h"

#include "esp_event.h"

typedef void* usr_esp_event_handler_instance_t;
typedef void* usr_gpio_handle_t;

typedef enum {
    WIFI_EVENT_BASE = 0,
    IP_EVENT_BASE,
} usr_esp_event_base_t;

#define GET_MACRO(_1, _2, _3, _4, _5, _6, _7, NAME, ...) NAME
#define EXECUTE_SYSCALL(...) GET_MACRO(__VA_ARGS__, \
        __syscall6, __syscall5, __syscall4, __syscall3, __syscall2, __syscall1, __syscall0)(__VA_ARGS__)

static inline uint32_t __syscall6(uint32_t arg0, uint32_t arg1,
                         uint32_t arg2, uint32_t arg3,
                         uint32_t arg4, uint32_t arg5,
                         uint32_t syscall_num)
{
    register uint32_t a0 asm ("a0") = arg0;
    register uint32_t a1 asm ("a1") = arg1;
    register uint32_t a2 asm ("a2") = arg2;
    register uint32_t a3 asm ("a3") = arg3;
    register uint32_t a4 asm ("a4") = arg4;
    register uint32_t a5 asm ("a5") = arg5;
    register uint32_t a7 asm ("a7") = syscall_num;

    asm volatile ("ecall"
              : "+r" (a0)
              : "r" (a1), "r" (a2), "r" (a3), "r" (a4), "r" (a5), "r" (a7)
              : "memory");
    return a0;
}

static inline uint32_t __syscall5(uint32_t arg0, uint32_t arg1,
                         uint32_t arg2, uint32_t arg3,
                         uint32_t arg4,
                         uint32_t syscall_num)
{
    register uint32_t a0 asm ("a0") = arg0;
    register uint32_t a1 asm ("a1") = arg1;
    register uint32_t a2 asm ("a2") = arg2;
    register uint32_t a3 asm ("a3") = arg3;
    register uint32_t a4 asm ("a4") = arg4;
    register uint32_t a7 asm ("a7") = syscall_num;

    asm volatile ("ecall"
              : "+r" (a0)
              : "r" (a1), "r" (a2), "r" (a3), "r" (a4), "r" (a7)
              : "memory");
    return a0;
}

static inline uint32_t __syscall4(uint32_t arg0, uint32_t arg1,
                         uint32_t arg2, uint32_t arg3,
                         uint32_t syscall_num)
{
    register uint32_t a0 asm ("a0") = arg0;
    register uint32_t a1 asm ("a1") = arg1;
    register uint32_t a2 asm ("a2") = arg2;
    register uint32_t a3 asm ("a3") = arg3;
    register uint32_t a7 asm ("a7") = syscall_num;

    asm volatile ("ecall"
              : "+r" (a0)
              : "r" (a1), "r" (a2), "r" (a3), "r" (a7)
              : "memory");
    return a0;
}

static inline uint32_t __syscall3(uint32_t arg0, uint32_t arg1,
                         uint32_t arg2,
                         uint32_t syscall_num)
{
    register uint32_t a0 asm ("a0") = arg0;
    register uint32_t a1 asm ("a1") = arg1;
    register uint32_t a2 asm ("a2") = arg2;
    register uint32_t a7 asm ("a7") = syscall_num;

    asm volatile ("ecall"
              : "+r" (a0)
              : "r" (a1), "r" (a2), "r" (a7)
              : "memory");
    return a0;
}

static inline uint32_t __syscall2(uint32_t arg0, uint32_t arg1,
                         uint32_t syscall_num)
{
    register uint32_t a0 asm ("a0") = arg0;
    register uint32_t a1 asm ("a1") = arg1;
    register uint32_t a7 asm ("a7") = syscall_num;

    asm volatile ("ecall"
              : "+r" (a0)
              : "r" (a1), "r" (a7)
              : "memory");
    return a0;
}

static inline uint32_t __syscall1(uint32_t arg0, uint32_t syscall_num)
{
    register uint32_t a0 asm ("a0") = arg0;
    register uint32_t a7 asm ("a7") = syscall_num;

    asm volatile ("ecall"
              : "+r" (a0)
              : "r" (a7)
              : "memory");
    return a0;
}

static inline uint32_t __syscall0(uint32_t syscall_num)
{
    register uint32_t a0 asm ("a0");
    register uint32_t a7 asm ("a7") = syscall_num;

    asm volatile ("ecall"
              : "+r" (a0)
              : "r" (a7)
              : "memory");
    return a0;
}

typedef void (*usr_esp_event_handler_t)(void* event_handler_arg,
                                        usr_esp_event_base_t event_base,
                                        int32_t event_id,
                                        void* event_data);

/**
 * @brief Register user space event handler
 *
 * @param event_base Base ID of the event
 * @param event_id ID of the event
 * @param event_handler Handler function that gets called when the event occurs
 * @param event_handler_arg Data passed to the handler
 * @param context Event handler instance object that contains the context.
 *
 * @return
 *      - ESP_OK on success
 *      - esp_err_t return code, otherwise
 *
 * @note
 *     esp_event_base_t is a unique string pointer which resides in the component that exposes it,
 *     it means it resides in the protected app and its value can only be determined after compiling
 *     protected application. Extracting the pointer and passing it to user app becomes tricky so instead
 *     of using the pointer, we have defined usr_esp_event_base_t which is an enum and the translation
 *     from enum to its string pointer is handled in esp_syscalls.c which has knowledge of both, the pointer
 *     and the enum
 */
esp_err_t usr_esp_event_handler_instance_register(usr_esp_event_base_t event_base,
                                              int32_t event_id,
                                              usr_esp_event_handler_t event_handler,
                                              void *event_handler_arg,
                                              usr_esp_event_handler_instance_t *context);

/**
 * @brief De-register user space event handler
 *
 * @param event_base Base ID of the event
 * @param event_id ID of the event
 * @param context Event handler instance object returned when registering the instance.
 *
 * @return
 *      - ESP_OK on success
 *      - esp_err_t return code, otherwise
 */
esp_err_t usr_esp_event_handler_instance_unregister(usr_esp_event_base_t event_base,
                                                    int32_t event_id,
                                                    usr_esp_event_handler_instance_t context);

/**
 * @brief Register User space GPIO Soft-ISR handler
 *
 * @param gpio_num GPIO numnber to which this handler is attached
 * @param softisr_handler User space soft-ISR handler which will be invoked when an interrupt occurs on the given GPIO
 * @param args  User specified argument
 * @param gpio_handle Handle attached to this soft-isr handler
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL otherwise
 *
 * @note
 *      User code is never executed in ISR context so there can't be user registered ISR handlers,
 *      we still have a provision that allows user code to register a soft-isr handler that works
 *      like a callback and is executed in a task's context
 */
esp_err_t gpio_softisr_handler_add(gpio_num_t gpio_num, gpio_isr_t softisr_handler, void *args, usr_gpio_handle_t *gpio_handle);

/**
 * @brief Remove User space GPIO Soft-ISR handler
 *
 * @param gpio_handle Handle for the previously added soft-isr handler
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL otherwise
 */
esp_err_t gpio_softisr_handler_remove(usr_gpio_handle_t gpio_handle);
