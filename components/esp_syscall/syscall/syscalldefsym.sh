#!/bin/sh

in="$1"

defsym_list=$(grep -E "^[0-9A-Fa-fXx]+[[:space:]]" "$in" | sort -n | (
	while read nr abi name entry ; do
        if [ "$abi" = "common" ];
        then
            list="${list}-Wl,--wrap=${name} -Wl,--defsym=__wrap_${name}=usr_${name} "
        fi
	done
    printf "%s" "${list}"
    ))

printf "%s" "${defsym_list}"
