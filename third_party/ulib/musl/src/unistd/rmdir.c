#include <unistd.h>

#include <errno.h>

#include "libc.h"

#if SHARED
static int io_rmdir(const char* path) {
    errno = EIO;
    return -1;
}
weak_alias(io_rmdir, __libc_io_rmdir);
#else
int __libc_io_rmdir(const char* path);
#endif

int rmdir(const char* path) {
    return __libc_io_rmdir(path);
}
