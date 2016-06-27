#include "syscall.h"
#include <unistd.h>

ssize_t pread(int fd, void* buf, size_t size, off_t ofs) {
    return syscall(SYS_pread, fd, buf, size, ofs);
}
