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

#include "xtensa_context.h"

#if defined(_ASMLANGUAGE) || defined(__ASSEMBLER__)
#define STRUCT_ALIGN(ctype,size,asname,name,n)  .align (n)
#else
#define STRUCT_ALIGN(ctype,size,asname,name,n)  ctype   name[size] __attribute__ ((aligned(n)));
#endif
/*
-------------------------------------------------------------------------------
  SYSCALL EXCEPTION STACK FRAME FOR A THREAD OR NESTED INTERRUPT

  This struct defines the way the registers are stored on the
  interrupt stack during a system call exception.
-------------------------------------------------------------------------------
*/
STRUCT_BEGIN
STRUCT_FIELD (long, 4, XT_ISTK_A0,          a0)
STRUCT_FIELD (long, 4, XT_ISTK_A1,          a1)
STRUCT_FIELD (long, 4, XT_ISTK_A2,          a2)
STRUCT_FIELD (long, 4, XT_ISTK_A3,          a3)
STRUCT_FIELD (long, 4, XT_ISTK_A4,          a4)
STRUCT_FIELD (long, 4, XT_ISTK_A5,          a5)
STRUCT_FIELD (long, 4, XT_ISTK_A6,          a6)
STRUCT_FIELD (long, 4, XT_ISTK_A7,          a7)
STRUCT_FIELD (long, 4, XT_ISTK_A8,          a8)
STRUCT_FIELD (long, 4, XT_ISTK_A9,          a9)
STRUCT_FIELD (long, 4, XT_ISTK_A10,         a10)
STRUCT_FIELD (long, 4, XT_ISTK_A11,         a11)
STRUCT_FIELD (long, 4, XT_ISTK_A12,         a12)
STRUCT_FIELD (long, 4, XT_ISTK_A13,         a13)
STRUCT_FIELD (long, 4, XT_ISTK_A14,         a14)
STRUCT_FIELD (long, 4, XT_ISTK_A15,         a15)
STRUCT_FIELD (long, 4, XT_ISTK_TMP0,        tmp0)
STRUCT_FIELD (long, 4, XT_ISTK_TMP1,        tmp1)
STRUCT_FIELD (long, 4, XT_ISTK_TMP2,        tmp2)

STRUCT_FIELD (long, 4, XT_ISTK_PC,          pc)
STRUCT_FIELD (long, 4, XT_ISTK_PS,          ps)
STRUCT_FIELD (long, 4, XT_ISTK_LBEG,        lbeg)
STRUCT_FIELD (long, 4, XT_ISTK_LEND,        lend)
STRUCT_FIELD (long, 4, XT_ISTK_LCOUNT,      lcount)
STRUCT_FIELD (long, 4, XT_ISTK_SAR,         sar)
STRUCT_FIELD (long, 4, XT_ISTK_SCOMPARE1,   scompare1)
STRUCT_FIELD (long, 4, XT_ISTK_STACK,       user_stack)
STRUCT_FIELD (long, 4, XT_ISTK_TOPOFSTACK,  user_topofstack)
STRUCT_END(XtSyscallExcFrame)

#if defined(_ASMLANGUAGE) || defined(__ASSEMBLER__)
#define XT_ISTK_NEXT1      XtSyscallExcFrameSize
#else
#define XT_ISTK_NEXT1      sizeof(XtSyscallExcFrame)
#endif

#define XT_ISTK_EXTRA      (ALIGNUP(XCHAL_EXTRA_SA_ALIGN, XT_ISTK_NEXT1) + XCHAL_EXTRA_SA_ALIGN)

/* If need more alignment than stack, add space for dynamic alignment */
#define XT_ISTK_EXTRA      (ALIGNUP(XCHAL_EXTRA_SA_ALIGN, XT_ISTK_NEXT1) + XCHAL_EXTRA_SA_ALIGN)
#define XT_ISTK_NEXT2      (XT_ISTK_EXTRA + XCHAL_EXTRA_SA_SIZE)

#define XT_ISTK_FRMSZ      (ALIGNUP(0x10, XT_ISTK_NEXT2) + 0x20)
