#!/bin/sh

in="$1"
out="$2"

emit() {
	_total="$1"
	_nr="$2"
	_entry="$3"

	while [ $_total -lt $_nr ]; do
		printf "__SYSCALL(%s, sys_ni_syscall, )\n" "${_total}"
		_total=$((_total+1))
	done
	printf "__SYSCALL(%s, %s, )\n" "${_nr}" "${_entry}"
}

dup=$(grep -wo "^[0-9]\+" "$in" | sort | uniq -d)
if [ $dup ]
then
    echo "********************************************************"
    echo "   ERROR: Found duplicate syscall numbers, exiting..."
    echo "********************************************************"
    exit 1
fi

grep -E "^[0-9A-Fa-fXx]+[[:space:]]" "$in" | sort -n | (
	total=0

	while read nr abi name entry ; do
		emit $((total)) $((nr)) $entry
		total=$((nr+1))
	done
) > "$out"
