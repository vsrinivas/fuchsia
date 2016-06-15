#include "syscall.h"
#include <sys/stat.h>

int mkdirat(int fd, const char* path, mode_t mode) {
    return syscall(SYS_mkdirat, fd, path, mode);
}
