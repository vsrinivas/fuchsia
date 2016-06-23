#include <unistd.h>

#include <errno.h>

#include "libc.h"

#if SHARED
int io_unlinkat(int fd, const char* path, int flag) {
    errno = EIO;
    return -1;
}
weak_alias(io_unlinkat, __libc_io_unlinkat);
#else
int __libc_io_unlinkat(int fd, const char* path, int flag);
#endif

int unlinkat(int fd, const char* path, int flag) {
    return __libc_io_unlinkat(fd, path, flag);
}
