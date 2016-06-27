#include "syscall.h"
#include <unistd.h>

ssize_t pwrite(int fd, const void* buf, size_t size, off_t ofs) {
    return syscall(SYS_pwrite, fd, buf, size, ofs);
}
