#!/bin/bash

TOOLCHAIN_PREFIX="$1"
IN_ELF_FILE="$2"
OUT_MEM_LAYOUT_FILE="$3"

sym_arr=(_reserve_w1_iram_start _reserve_w1_iram_end _reserve_w1_dram_start _reserve_w1_dram_end)
def_arr=(USER_IRAM_START USER_IRAM_END USER_DRAM_START USER_DRAM_END)

printf "/* Memory layout is auto generated file from the protected_app build system */\n\n" > "$OUT_MEM_LAYOUT_FILE"

for (( i=0; i<${#sym_arr[@]}; i++ ));
do
    echo "#define ${def_arr[$i]} 0x$($TOOLCHAIN_PREFIX"nm" $IN_ELF_FILE | grep ${sym_arr[$i]} | awk '{print $1}')" >> "$OUT_MEM_LAYOUT_FILE"
done
