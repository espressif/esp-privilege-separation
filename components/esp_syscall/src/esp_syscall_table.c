#include "syscall_def.h"
#include "syscall_dec.h"

typedef void (*syscall_t)(void);

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
#endif
const syscall_t syscall_table[__NR_syscalls] = {
    [0 ... __NR_syscalls - 1] = (syscall_t)&sys_ni_syscall,

#define __SYSCALL(nr, symbol, nargs)  [nr] = (syscall_t)symbol,
#include "esp_syscall.h"
};
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
