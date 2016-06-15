#include "libc.h"
#include "syscall.h"
#include <unistd.h>

ssize_t write(int fd, const void* buf, size_t count) {
    return syscall(SYS_write, fd, buf, count);
}
