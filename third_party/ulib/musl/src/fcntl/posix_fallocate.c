#include "libc.h"
#include "syscall.h"
#include <fcntl.h>

int posix_fallocate(int fd, off_t base, off_t len) {
    return -__syscall(SYS_fallocate, fd, 0, base, len);
}

LFS64(posix_fallocate);
