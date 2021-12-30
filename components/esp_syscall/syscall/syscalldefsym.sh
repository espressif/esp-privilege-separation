#!/bin/sh

syscall_tbl="$1"
override_tbl="$2"

syscall_defsym_list=$(grep -E "^[0-9A-Fa-fXx]+[[:space:]]" "$syscall_tbl" | sort -n | (
	while read nr abi name entry ; do
        if [ "$abi" = "common" ];
        then
            list="${list}-Wl,--wrap=${name} -Wl,--defsym=__wrap_${name}=usr_${name} "
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
