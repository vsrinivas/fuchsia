#include "libc.h"
#include "syscall.h"
#include <unistd.h>

int ftruncate(int fd, off_t length) {
    return syscall(SYS_ftruncate, fd, length);
}

LFS64(ftruncate);
