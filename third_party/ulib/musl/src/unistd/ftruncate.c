#include "syscall.h"
#include <unistd.h>

int ftruncate(int fd, off_t length) {
    return syscall(SYS_ftruncate, fd, length);
}
