#include "libc.h"
#include "syscall.h"
#include <unistd.h>

ssize_t read(int fd, void* buf, size_t count) {
    return syscall(SYS_read, fd, buf, count);
}
