#include "libc.h"
#include "syscall.h"
#include <fcntl.h>
#include <sys/stat.h>

int stat(const char* restrict path, struct stat* restrict buf) {
#ifdef SYS_stat
    return syscall(SYS_stat, path, buf);
#else
    return syscall(SYS_fstatat, AT_FDCWD, path, buf, 0);
#endif
}

LFS64(stat);
