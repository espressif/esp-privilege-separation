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

#pragma once

#include "riscv/rvruntime-frames.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ECALL stack frame
 */
STRUCT_BEGIN
STRUCT_FIELD (long, 4, RV_ESTK_SP,          sp)
STRUCT_FIELD (long, 4, RV_ESTK_STACK,       stack)
STRUCT_FIELD (long, 4, RV_ESTK_TOPOFSTACK,  top_of_stack)
STRUCT_FIELD (long, 4, RV_ESTK_INT_THRESH,  int_thres)
STRUCT_FIELD (long, 4, RV_ESTK_MCAUSE,      mcause)
STRUCT_FIELD (long, 4, RV_ESTK_MEPC,        mepc)
STRUCT_FIELD (long, 4, RV_ESTK_MSTATUS,     mstatus)
STRUCT_FIELD (long, 4, RV_ESTK_MTVAL,       mtval)
STRUCT_FIELD (long, 4, RV_ESTK_MHARTID,     mhartid)
STRUCT_END(RvEcallFrame)

#if defined(_ASMLANGUAGE) || defined(__ASSEMBLER__)
#define RV_ESTK_SIZE     RvEcallFrameSize
#else
#define RV_ESTK_SIZE     sizeof(RvEcallFrame)
#endif

#define RV_ESTK_FRMSZ    (ALIGNUP(0x10, RV_ESTK_SIZE))

#ifdef __cplusplus
}
#endif
