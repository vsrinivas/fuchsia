#include "syscall.h"
#include <unistd.h>

int fdatasync(int fd) {
    return syscall(SYS_fdatasync, fd);
}
