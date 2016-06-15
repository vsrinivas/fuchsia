#include "syscall.h"
#include <unistd.h>

int unlinkat(int fd, const char* path, int flag) {
    return syscall(SYS_unlinkat, fd, path, flag);
}
