#include "syscall.h"
#include <fcntl.h>
#include <unistd.h>

int unlink(const char* path) {
#ifdef SYS_unlink
    return syscall(SYS_unlink, path);
#else
    return syscall(SYS_unlinkat, AT_FDCWD, path, 0);
#endif
}
