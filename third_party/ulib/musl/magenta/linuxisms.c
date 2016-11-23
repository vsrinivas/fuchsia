#include "debug.h"
#include "syscall.h"

#include <errno.h>

long __linux_syscall(const char* fn, int ln, syscall_arg_t nr, ...) {
    warn_unsupported("\nWARNING: %s: %d: Linux Syscalls Not Supported (#%ld)\n", fn, ln, nr);
    return -ENOSYS;
}
