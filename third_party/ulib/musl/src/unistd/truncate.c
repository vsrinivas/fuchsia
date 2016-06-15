#include "libc.h"
#include "syscall.h"
#include <unistd.h>

int truncate(const char* path, off_t length) {
    return syscall(SYS_truncate, path, length);
}

LFS64(truncate);
