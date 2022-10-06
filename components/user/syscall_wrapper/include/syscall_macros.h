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
#include <stdint.h>

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

