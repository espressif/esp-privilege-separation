#!/bin/sh

in="$1"
out="$2"

fileguard=_SYSCALL_DEF_H

grep -E "^[0-9A-Fa-fXx]+[[:space:]]" "$in" | sort -n | (
	printf "#ifndef %s\n" "${fileguard}"
	printf "#define %s\n" "${fileguard}"
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
	printf "#endif /* %s */" "${fileguard}"
) > "$out"
