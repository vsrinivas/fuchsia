#include "syscall.h"
#include <unistd.h>

int fsync(int fd) {
    return syscall(SYS_fsync, fd);
}
