#include "libc.h"
#include <errno.h>
#include <magenta/syscalls.h>
#include <sys/mman.h>

static void dummy(void) {}
weak_alias(dummy, __vm_wait);

int __munmap(void* start, size_t len) {
    __vm_wait();

    uintptr_t ptr = (uintptr_t)start;
    /* NOTE: this currently unmaps the entire region that start was mapped into.
     * magenta does not yet support partial unmapping.
     */
    mx_status_t status = _mx_vmar_unmap(_mx_vmar_root_self(), ptr, 0);
    if (status < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

weak_alias(__munmap, munmap);
