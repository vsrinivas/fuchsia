#define _GNU_SOURCE
#include "syscall.h"
#include <sys/mman.h>

int mincore(void* addr, size_t len, unsigned char* vec) {
    return syscall(SYS_mincore, addr, len, vec);
}
