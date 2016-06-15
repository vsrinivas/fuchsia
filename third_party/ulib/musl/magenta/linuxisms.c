#include "debug.h"
#include "syscall.h"

long __linux_syscall(const char* fn, int ln, syscall_arg_t nr, ...) {
    panic("\nFATAL: %s: %d: Linux Syscalls Not Supported (#%ld)\n", fn, ln, nr);
}
