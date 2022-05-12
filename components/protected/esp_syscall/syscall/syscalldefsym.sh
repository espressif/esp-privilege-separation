#!/bin/sh

# syscalldefsym.sh generates the list of defsyms required for user app.
# This script helps to remove usr_ prefix for all the specified symbols.

# syscall_tbl is the table which actually stores system call entries.
# Where as override_tbl is used to map one symbol to another.
# Hence, we generate defsym entries for both the tables.
syscall_tbl="$1"    # Path to syscall.tbl file
override_tbl="$2"   # Path to override.tbl file

syscall_defsym_list=$(grep -E "^[0-9A-Fa-fXx]+[[:space:]]" "$syscall_tbl" | sort -n | (
	while read nr abi name entry ; do
        if [ "$abi" = "common" ];
        then
            list="${list}-u usr_${name} -Wl,--wrap=${name} -Wl,--defsym=__wrap_${name}=usr_${name} "
        fi
	done
    printf "%s" "${list}"
    ))

override_defsym_list=$(grep -E "^[0-9A-Fa-fXx]+[[:space:]]" "$override_tbl" | sort -n | (
	while read nr abi name entry ; do
        if [ "$abi" = "common" ];
        then
            list="${list}-Wl,--wrap=${name} -Wl,--defsym=__wrap_${name}=${entry} "
        fi
	done
    printf "%s" "${list}"
    ))

printf "%s" "${syscall_defsym_list}" "${override_defsym_list}"
