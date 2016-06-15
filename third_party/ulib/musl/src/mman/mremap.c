#define _GNU_SOURCE
#include "libc.h"
#include "syscall.h"
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

static void dummy(void) {}
weak_alias(dummy, __vm_wait);

void* __mremap(void* old_addr, size_t old_len, size_t new_len, int flags, ...) {
    va_list ap;
    void* new_addr = 0;

    if (new_len >= PTRDIFF_MAX) {
        errno = ENOMEM;
        return MAP_FAILED;
    }

    if (flags & MREMAP_FIXED) {
        __vm_wait();
        va_start(ap, flags);
        new_addr = va_arg(ap, void*);
        va_end(ap);
    }

    // TODO(kulakowski) Implement more mmap
    return NULL;
}

weak_alias(__mremap, mremap);
