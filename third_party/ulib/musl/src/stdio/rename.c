#include "syscall.h"
#include <fcntl.h>
#include <stdio.h>

int rename(const char* old, const char* new) {
#ifdef SYS_rename
    return syscall(SYS_rename, old, new);
#else
    return syscall(SYS_renameat, AT_FDCWD, old, AT_FDCWD, new);
#endif
}
