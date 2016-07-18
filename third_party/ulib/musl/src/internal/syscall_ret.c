#include "syscall.h"
#include <errno.h>

__attribute__((__visibility__("hidden"))) long __syscall_ret(unsigned long r) {
    if (r > -4096UL) {
        errno = -r;
        return -1;
    }
    return r;
}
