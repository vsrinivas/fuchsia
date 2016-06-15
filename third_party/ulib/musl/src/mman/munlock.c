#include "syscall.h"
#include <sys/mman.h>

int munlock(const void* addr, size_t len) {
    return syscall(SYS_munlock, addr, len);
}
