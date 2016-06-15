#include "syscall.h"
#include <unistd.h>

ssize_t readlinkat(int fd, const char* restrict path, char* restrict buf, size_t bufsize) {
    return syscall(SYS_readlinkat, fd, path, buf, bufsize);
}
