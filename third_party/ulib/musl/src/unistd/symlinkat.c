#include "syscall.h"
#include <unistd.h>

int symlinkat(const char* existing, int fd, const char* new) {
    return syscall(SYS_symlinkat, existing, fd, new);
}
