#include "libc.h"
#include "syscall.h"
#include <sys/mman.h>

static void dummy(void) {}
weak_alias(dummy, __vm_wait);

int __munmap(void* start, size_t len) {
    __vm_wait();
    // TODO(kulakowski) Implement more mmap
    return 0;
}

weak_alias(__munmap, munmap);
