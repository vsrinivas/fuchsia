#include "syscall.h"
#include <sys/file.h>

int flock(int fd, int op) {
    return syscall(SYS_flock, fd, op);
}
