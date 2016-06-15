#include "libc.h"
#include "syscall.h"
#include <sys/mman.h>

int __madvise(void* addr, size_t len, int advice) {
    // TODO(kulakowski) Implement more mmap
    return 0;
}

weak_alias(__madvise, madvise);
