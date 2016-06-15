#include "syscall.h"
#include <unistd.h>

int dup(int fd) {
    return syscall(SYS_dup, fd);
}
