#include "libc.h"
#include "syscall.h"
#include <dirent.h>

int __getdents(int fd, struct dirent* buf, size_t len) {
    return syscall(SYS_getdents, fd, buf, len);
}

weak_alias(__getdents, getdents);
