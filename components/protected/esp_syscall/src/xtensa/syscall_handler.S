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

#include "xtensa_rtos.h"
#include "esp_private/panic_reason.h"
#include "sdkconfig.h"
#include "soc/soc.h"
#include "soc/sensitive_reg.h"
#include "soc/world_controller_reg.h"
#include "soc/extmem_reg.h"
#include "soc_defs.h"
#include "syscall_def.h"
#include "syscall_context.h"

/*
 * uxTCBNumber and uxTaskNumber members are added in TCB structure when
 * FreeRTOS Trace facility is enabled. TLS pointer offset is changed by 8 bytes
 * because both members are 4 bytes in size.
 */
#ifdef CONFIG_FREERTOS_USE_TRACE_FACILITY
#define TLS_POINTER_OFFSET      (0x48 + CONFIG_FREERTOS_MAX_TASK_NAME_LEN + 3 + 8 + 4)&~3
#else
#define TLS_POINTER_OFFSET      (0x48 + CONFIG_FREERTOS_MAX_TASK_NAME_LEN + 3 + 4)&~3
#endif

#define TOPOFSTACK_OFFS         0x00
#define USERSTACK_OFFSET        0x30 + 4

.extern pxCurrentTCB

    .section .iram1,"ax"
    .type       _syscall_context_save,@function
    .align      4

_syscall_context_save:
    s32i    a2,  sp, XT_ISTK_A2
    s32i    a3,  sp, XT_ISTK_A3
    s32i    a4,  sp, XT_ISTK_A4
    s32i    a5,  sp, XT_ISTK_A5
    s32i    a6,  sp, XT_ISTK_A6
    s32i    a7,  sp, XT_ISTK_A7
    s32i    a8,  sp, XT_ISTK_A8
    s32i    a9,  sp, XT_ISTK_A9
    s32i    a10, sp, XT_ISTK_A10
    s32i    a11, sp, XT_ISTK_A11
    s32i    a12, sp, XT_ISTK_A12
    s32i    a13, sp, XT_ISTK_A13
    s32i    a14, sp, XT_ISTK_A14
    s32i    a15, sp, XT_ISTK_A15

    rsr     a3,  SAR
    s32i    a3,  sp, XT_ISTK_SAR
    rsr     a3,  LBEG
    s32i    a3,  sp, XT_ISTK_LBEG
    rsr     a3,  LEND
    s32i    a3,  sp, XT_ISTK_LEND
    rsr     a3,  LCOUNT
    s32i    a3,  sp, XT_ISTK_LCOUNT

#if XCHAL_HAVE_S32C1I
    rsr     a3, SCOMPARE1
    s32i    a3, a1, XT_ISTK_SCOMPARE1
#endif

    mov     a9,  a0                    /* preserve ret addr */

    s32i    a12, sp, XT_ISTK_TMP0      /* temp. save stuff in stack frame */
    s32i    a13, sp, XT_ISTK_TMP1
    s32i    a9,  sp, XT_ISTK_TMP2

    l32i    a12, sp, XT_ISTK_A12       /* recover original a9,12,13 */
    l32i    a13, sp, XT_ISTK_A13
    l32i    a9,  sp, XT_ISTK_A9

    rsr     a2, PS                     /* to be restored after spilling live registers */
    movi    a0, PS_INTLEVEL_MASK
    and     a3, a2, a0                 /* get the current INTLEVEL */
    bgeui   a3, XCHAL_EXCM_LEVEL, 2f   /* calculate max(INTLEVEL, XCHAL_EXCM_LEVEL) */
    movi    a3, XCHAL_EXCM_LEVEL
2:
    movi    a0, PS_UM | PS_WOE         /* clear EXCM, enable window overflow, set new INTLEVEL */
    or      a3, a3, a0
    wsr     a3, ps
    rsr     a0, EPC1                   /* to be restored after spilling live registers */

    /* Currently we are on kernel stack, switch the stack pointer back to user space stack for spilling live registers.
     * This will save us the trouble of copying the base save area from kernel stack to user stack while handing over
     * the control back to user space
     *
     * Use DEPC register to hold the current kernel stack pointer temporarily
     */
    wsr     a1, DEPC
    l32i    a1,  sp, XT_ISTK_A1
    and a12, a12, a12
    rotw 3
    and a12, a12, a12
    rotw 3
    and a12, a12, a12
    rotw 3
    and a12, a12, a12
    rotw 3
    and a12, a12, a12
    rotw 4

    /* Restore the kernel stack pointer */
    rsr     a1, DEPC

    wsr     a2, PS                     /* restore to the value at entry */
    rsync
    wsr     a0, EPC1                   /* likewise */

    l32i    a12, sp, XT_ISTK_TMP0      /* restore the temp saved registers */
    l32i    a13, sp, XT_ISTK_TMP1      /* our return address is there */
    l32i    a9,  sp, XT_ISTK_TMP2

    mov     a0, a9                     /* retrieve ret addr */
    ret



    .section .iram1,"ax"
    .type       _syscall_context_restore,@function
    .align      4

_syscall_context_restore:
    l32i    a2,  sp, XT_ISTK_LBEG
    l32i    a3,  sp, XT_ISTK_LEND
    wsr     a2,  LBEG
    wsr     a3,  LEND
    l32i    a2,  sp, XT_ISTK_LCOUNT
    wsr     a2,  LCOUNT
    l32i    a3,  sp, XT_ISTK_SAR
    wsr     a3,  SAR
#if XCHAL_HAVE_S32C1I
    l32i    a2,  sp, XT_ISTK_SCOMPARE1
    wsr     a2,  SCOMPARE1
#endif

    l32i    a2,  sp, XT_ISTK_A2
    l32i    a3,  sp, XT_ISTK_A3
    l32i    a4,  sp, XT_ISTK_A4
    l32i    a5,  sp, XT_ISTK_A5
    l32i    a6,  sp, XT_ISTK_A6
    l32i    a7,  sp, XT_ISTK_A7
    l32i    a8,  sp, XT_ISTK_A8
    l32i    a9,  sp, XT_ISTK_A9
    l32i    a10, sp, XT_ISTK_A10
    l32i    a11, sp, XT_ISTK_A11
    l32i    a12, sp, XT_ISTK_A12
    l32i    a13, sp, XT_ISTK_A13
    l32i    a14, sp, XT_ISTK_A14
    l32i    a15, sp, XT_ISTK_A15

    ret

/*
--------------------------------------------------------------------------------
Syscall Exception Handler (jumped to from User Exception Handler).
Syscall 0 is required to spill the register windows (no-op in Call 0 ABI).
Only syscall 0 is handled here. Other syscalls return -1 to caller in a2.
--------------------------------------------------------------------------------
*/

    .global     _xt_syscall_exc
    .section .iram1,"ax"
    .type       _xt_syscall_exc,@function
    .align      4
_xt_syscall_exc:
    /* Use DEPC as a temporary register to hold content of a3 register */
    wsr     a3, DEPC

    movi    a3, pxCurrentTCB
    l32i    a3, a3, 0
    /* Load the Kernel stack in a0 */
    l32i    a0, a3, TLS_POINTER_OFFSET
    /* Move to the bottom of the stack */
    addi    a0, a0, KERNEL_STACK_SIZE
    /* Allocate syscall stack frame */
    addi    a0, a0, -XT_ISTK_FRMSZ

    /* Save the user stack pointer on the kernel stack */
    s32i    a1, a0, XT_ISTK_A1

    /* Switch to kernel stack */
    mov     a1, a0

    /* Save interrupted PS reg */
    rsr     a3, PS
    s32i    a3, a0, XT_ISTK_PS

    /* Save EPC_1 reg
     * EPC_1 contains the address of syscall instruction,
     * once we finish the system call we should jump to the next
     * instruction after syscall.
     * `syscall` instruction is of 3 bytes so add 3 to EPC_1 address
     */
    rsr     a3, EPC_1
    addi    a3, a3, 3
    s32i    a3, a0, XT_ISTK_PC

    /* Save the return address of the interrupted thread */
    rsr     a3, EXCSAVE_1
    s32i    a3, a0, XT_ISTK_A0

    /* Restore a3 register from DEPC */
    rsr     a3, DEPC

    /* Save rest of the user context on the kernel stack */
    call0 _syscall_context_save

    /* Store user stack and user top of stack */
    movi    a3, pxCurrentTCB
    l32i    a3, a3, 0
    /* Get user stack */
    l32i    a2, a3, USERSTACK_OFFSET
    s32i    a2, a1, XT_ISTK_STACK
    /* Get user top of stack */
    l32i    a2, a3, TOPOFSTACK_OFFS
    s32i    a2, a1, XT_ISTK_TOPOFSTACK
    /* Replace stack with the kernel stack in TCB */
    l32i    a2, a3, TLS_POINTER_OFFSET
    s32i    a2, a3, USERSTACK_OFFSET
    /* Replace top of stack with current SP in TCB */
    s32i    a1, a3, TOPOFSTACK_OFFS

    /* Preserve the interrupt level that was at the time of the syscall
     * syscall can be invoked even when the interrupts are disabled.
     * Ensure that they stay that way
     */
    l32i    a4, sp, XT_ISTK_PS
    movi    a3, PS_INTLEVEL_MASK
    and     a3, a3, a4
    movi    a2, PS_UM | PS_WOE
    or      a2, a2, a3
    wsr     a2, PS
    rsync

    /*
    Go to system call handler. Set up parameters to pass to the
    exception handler and call the exception handler.
    */
    mov     a6, a1
    movi    a4, _esp_syscall_dispatcher
    callx4  a4

    /* Extract the INTLEVEL after system call
     * We have system calls for xPortEnterCritical and vPortExitCritical
     * which modify the interrupt level inside the system call.
     * This should be passed onto the user space as well
     */
    rsr     a2, PS
    movi    a3, PS_INTLEVEL_MASK
    and     a2, a2, a3

    l32i    a3, sp, XT_ISTK_PS
    movi    a4, ~PS_INTLEVEL_MASK
    and     a3, a3, a4
    or      a3, a2, a3

    /* Keep the interrupts disabled by setting EXCM=1 */
    movi    a2, PS_EXCM
    or      a3, a3, a2

    wsr     a3, PS
    rsync

    /* Prepare WORLD for switch */
    movi    a3, 2
    /*TODO: Decide register based on core in case of dual core setup */
    movi    a2, WORLD_CONTROLLER_WCL_CORE_0_WORLD_PREPARE_REG
    s32i    a3, a2, 0

    /* Set TRIGGER_ADDR to the return address*/
    l32i    a2, sp, XT_ISTK_PC
    movi    a3, WORLD_CONTROLLER_WCL_CORE_0_WORLD_TRIGGER_ADDR_REG
    s32i    a2, a3, 0

    movi    a3, 1
    /*TODO: Decide register based on core in case of dual core setup */
    movi    a2, WORLD_CONTROLLER_WCL_CORE_0_WORLD_UPDATE_REG
    s32i    a3, a2, 0

    memw

    /* Restore user stack and user top of stack */
    movi    a3, pxCurrentTCB
    l32i    a3, a3, 0
    /* Get user stack */
    l32i    a2, sp, XT_ISTK_STACK
    s32i    a2, a3, USERSTACK_OFFSET
    /* Get user top of stack */
    l32i    a2, sp, XT_ISTK_TOPOFSTACK
    s32i    a2, a3, TOPOFSTACK_OFFS

    /* Restore PC */
    l32i    a2, sp, XT_ISTK_PC
    wsr     a2, EPC_1
    rsync

    call0   _syscall_context_restore

    l32i    a0, sp, XT_ISTK_A0
    l32i    a1, sp, XT_ISTK_A1

    rfe


/* Executes in Non-ISR context */
    .global     syscall_table

    .section .iram1,"ax"
    .align      4
_esp_syscall_dispatcher:

    entry   a1, 32

    /* syscall request code */
    l32i    a3, a2, XT_ISTK_A2

    movi    a4, __NR_syscalls
    bgeu    a3, a4, .invalid_syscall_number

    /* Load args: arg0 - arg5 are passed via regs. */
    l32i    a10, a2, XT_ISTK_A10
    l32i    a11, a2, XT_ISTK_A3
    l32i    a12, a2, XT_ISTK_A4
    l32i    a13, a2, XT_ISTK_A5
    l32i    a14, a2, XT_ISTK_A6
    l32i    a15, a2, XT_ISTK_A7

    movi    a4, syscall_table
    addx4   a4, a3, a4
    l32i    a4, a4, 0

    callx8  a4

    /* Store the return value */
    s32i    a10, a2, XT_ISTK_A2

.invalid_syscall_number:
    retw



     .global force_linker_to_include
force_linker_to_include:

