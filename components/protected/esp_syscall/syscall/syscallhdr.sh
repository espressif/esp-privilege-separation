#!/bin/sh

syscall_tbl="$1"        # [in] Path to syscall.tbl generated in build directory
syscall_def_h="$2"      # [out] Path to syscall_def.h header file
syscall_dec_h="$3"      # [out] Path to syscall_dec.h header file

grep -E "^[0-9A-Fa-fXx]+[[:space:]]" "$syscall_tbl" | sort -n | (
    printf "/**\n"
    printf "  * This header file is used to generate syscall number macros.\n"
    printf "  *\n"
    printf "  * This is AUTO GENERATED header file, please do not MODIFY!\n"
    printf "  */\n\n"
    printf "#pragma once"
    printf "\n"

    total=0
    while read nr abi name entry ; do
        printf "#define __NR_%s\t%s\n" \
                "${name}" "${nr}"
        total=$((nr+1))
    done

    printf "\n"
    printf "#define __NR_syscalls\t%s\n" "${total}"
    printf "\n"
) > "$syscall_def_h"

grep -E "^[0-9A-Fa-fXx]+[[:space:]]" "$syscall_tbl" | sort -n | (
    printf "/**\n"
    printf "  * This header file is used to provide function declarations\n"
    printf "  * for compiling esp_syscall_table.c source file. Please do not\n"
    printf "  * use it in application.\n"
    printf "  *\n"
    printf "  * This is AUTO GENERATED header file, please do not MODIFY!\n"
    printf "  */\n\n"
    printf "#pragma once\n"
    printf "#ifdef __cplusplus\n"
    printf "extern \"C\" {\n"
    printf "#endif"
    printf "\n"

    while read nr abi name entry ; do
        printf "void %s(void);\n" \
                "${entry}"
    done

    printf "\n"
    printf "#ifdef __cplusplus\n"
    printf "}\n"
    printf "#endif"
    printf "\n"
) > "$syscall_dec_h"
