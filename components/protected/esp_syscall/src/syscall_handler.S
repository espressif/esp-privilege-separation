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

#include "sdkconfig.h"
#include "soc/soc.h"
#include "soc/soc_caps.h"
#include "soc/interrupt_reg.h"
#include "riscv/rvruntime-frames.h"
#include "soc/sensitive_reg.h"
#include "soc/world_controller_reg.h"
#include "soc/extmem_reg.h"

#include "syscall_def.h"
#include "ecall_context.h"
#include "soc_defs.h"

#define EXCCAUSE_ECALL          11
#define TOPOFSTACK_OFFS         0x00
#define USERSTACK_OFFSET        0x30

/*
 * uxTCBNumber and uxTaskNumber members are added in TCB structure when
 * FreeRTOS Trace facility is enabled. TLS pointer offset is changed
 * by 8 bytes because both members are 4 bytes in size.
 */
#ifdef CONFIG_FREERTOS_USE_TRACE_FACILITY
#define TLS_POINTER_OFFSET      (0x48 + 16 + 3 + 8)&~3
#else
#define TLS_POINTER_OFFSET      (0x48 + 16 + 3)&~3
#endif

#define MPIE_BIT                3

    .global __ecall_handler
    .type __ecall_handler, @function
__ecall_handler:
    /* system call number : a7
     * Arguments: a0-a6
     * Arguments are already in a0-a6 and saved on user stack, no need to load them from stack
     */
    la      t2, pxCurrentTCB
    lw      t3, 0x00(t2)
    lw      t4, TLS_POINTER_OFFSET(t3)
    /* Move to the bottom of the stack and allocate syscall stack frame */
    li      t5, KERNEL_STACK_SIZE - RV_ESTK_FRMSZ
    add     t4, t4, t5

    sw      sp, RV_ESTK_SP(t4)
    /* Switch to kernel stack */
    mv      sp, t4

    /* Store user stack and user top of stack */
    lw      t3, 0x00(t2)
    /* Get user stack */
    lw      t4, USERSTACK_OFFSET(t3)
    sw      t4, RV_ESTK_STACK(sp)
    /* Get user top of stack */
    lw      t5, TOPOFSTACK_OFFS(t3)
    sw      t5, RV_ESTK_TOPOFSTACK(sp)
    /* Replace stack with the kernel stack in TCB */
    lw      t6, TLS_POINTER_OFFSET(t3)
    sw      t6, USERSTACK_OFFSET(t3)
    /* Replace top of stack with current SP in TCB */
    sw      sp, TOPOFSTACK_OFFS(t3)

    /* Save privilege registers */
    csrr    t2, mcause
    /* MEPC + 4 is the next instruction after ecall */
    csrr    t3, mepc
    addi    t3, t3, 4
    csrr    t4, mhartid
    csrr    t5, mstatus
    csrr    t6, mtval
    sw      t2, RV_ESTK_MCAUSE(sp)
    sw      t3, RV_ESTK_MEPC(sp)
    sw      t4, RV_ESTK_MHARTID(sp)
    sw      t5, RV_ESTK_MSTATUS(sp)
    sw      t6, RV_ESTK_MTVAL(sp)

    /* Save interrupt threshold level */
    /* No need for saving/restoring the interrupt threshold
     * as the ecall handler is not an interrupt but an exception.
     * ecall handler simply enables/disables the global interrupts.
     * The system call runs in the non ISR context so no need to increase the interrupt threshold
     */
    /*
    la      t2, INTERRUPT_CORE0_CPU_INT_THRESH_REG
    lw      t3, 0x00(t2)
    sw      t3, RV_ESTK_INT_THRESH(sp)
    */

    /* Enable WC-MIE */
    la      t2, WORLD_CONTROL_CORE_X_MSTATUS_MIE(0)
    li      t3, 1
    sw      t3, 0x0(t2)

    /* Enable interrupts and call the requested system call */
    li      t2, (1 << MPIE_BIT)
    csrs    mstatus, t2
    fence

    /* Check if valid syscall number */
    li      t2, __NR_syscalls
    bgt     a7, t2, .skip_syscall

#ifdef CONFIG_ESP_SYSCALL_VERIFY_RETURNED_POINTERS
    /* Save syscall number in s2 */
    mv      s2, a7
#endif

    la      t3, syscall_table
    slli    t4, a7, 2
    add     t3, t3, t4
    lw      t3, 0x0(t3)

    jalr    t3

#ifdef CONFIG_ESP_SYSCALL_VERIFY_RETURNED_POINTERS
    /* Load a0 and a1 with arguments for sys_verify_returned_ptr */
    mv      s3, a0
    mv      s4, a1
    mv      a1, s2
    la      t3, sys_verify_returned_ptr

    jalr    t3

    /* Load actual values returned from syscall */
    mv      a0, s3
    mv      a1, s4
#endif

.skip_syscall:
   /* Disable interrupts and restore user space */
    li      t2, (1 << MPIE_BIT)
    csrc    mstatus, t2

    /* restore the interrupt threshold level */
    /* See comment above saving/restoring threshold
    la      t3, INTERRUPT_CORE0_CPU_INT_THRESH_REG
    lw      t4, RV_ESTK_INT_THRESH(sp)
    sw      t4, 0x0(t3)
    */
    fence

    /* Restore machine registers */
    lw      t2, RV_ESTK_MCAUSE(sp)
    lw      t3, RV_ESTK_MEPC(sp)
    //lw      t4, RV_ESTK_MHARTID(sp)
    lw      t5, RV_ESTK_MSTATUS(sp)
    lw      t6, RV_ESTK_MTVAL(sp)
    csrw    mcause, t2
    csrw    mepc, t3
    //csrw    mhartid, t4
    csrw    mstatus, t5
    //csrw    mtval, t6

    /* Restore user stack and user top of stack */
    la      t2, pxCurrentTCB
    lw      t3, 0x00(t2)
    /* Get user stack */
    lw      t4, RV_ESTK_STACK(sp)
    sw      t4, USERSTACK_OFFSET(t3)
    /* Get user top of stack */
    lw      t5, RV_ESTK_TOPOFSTACK(sp)
    sw      t5, TOPOFSTACK_OFFS(t3)

    /* Prepare WC to switch to WORLD1 when returning back to user */
    la      t2, WORLD_CONTROL_CORE_X_WORLD_PREPARE(0)
    li      t3, 0x2
    sw      t3, 0x00(t2)

    /* Set TRIGGER_ADDR to the next instruction after ecall; held by MEPC */
    csrr    t4, mepc
    la      t5, WORLD_CONTROL_CORE_X_WORLD_TRIGGER_ADDR(0)
    sw      t4, 0x0(t5)

    la      t2, WORLD_CONTROL_CORE_X_WORLD_UPDATE(0)
    // t3 already has value 1 stored, use it
    sw      t3, 0x00(t2)
    fence

    /* Switch to user stack */
    lw      t2, RV_ESTK_SP(sp)
    mv      sp, t2

    /* Restore registers */
    lw ra,  RV_STK_RA(sp)
    lw gp,  RV_STK_GP(sp)
    lw tp,  RV_STK_TP(sp)
    lw t0,  RV_STK_T0(sp)
    lw t1,  RV_STK_T1(sp)
    lw t2,  RV_STK_T2(sp)
    lw s0,  RV_STK_S0(sp)
    lw s1,  RV_STK_S1(sp)
    /* A0 contains the return value from the system call */
    //lw a0,  RV_STK_A0(sp)
    lw a1,  RV_STK_A1(sp)
    lw a2,  RV_STK_A2(sp)
    lw a3,  RV_STK_A3(sp)
    lw a4,  RV_STK_A4(sp)
    lw a5,  RV_STK_A5(sp)
    lw a6,  RV_STK_A6(sp)
    lw a7,  RV_STK_A7(sp)
    lw s2,  RV_STK_S2(sp)
    lw s3,  RV_STK_S3(sp)
    lw s4,  RV_STK_S4(sp)
    lw s5,  RV_STK_S5(sp)
    lw s6,  RV_STK_S6(sp)
    lw s7,  RV_STK_S7(sp)
    lw s8,  RV_STK_S8(sp)
    lw s9,  RV_STK_S9(sp)
    lw s10, RV_STK_S10(sp)
    lw s11, RV_STK_S11(sp)
    lw t3,  RV_STK_T3(sp)
    lw t4,  RV_STK_T4(sp)
    lw t5,  RV_STK_T5(sp)
    lw t6,  RV_STK_T6(sp)

    lw sp,  RV_STK_SP(sp)

    /* Return back to user space.
     * mret will reenable the interrupts
     */
    mret
