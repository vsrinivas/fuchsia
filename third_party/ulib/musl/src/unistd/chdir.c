#include "syscall.h"
#include <unistd.h>

int chdir(const char* path) {
    return syscall(SYS_chdir, path);
}
