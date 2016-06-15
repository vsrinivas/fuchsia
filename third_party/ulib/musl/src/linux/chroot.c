#define _GNU_SOURCE
#include "syscall.h"
#include <unistd.h>

int chroot(const char* path) {
    return syscall(SYS_chroot, path);
}
