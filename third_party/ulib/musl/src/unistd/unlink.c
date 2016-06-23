#include <unistd.h>

#include <errno.h>

#include "libc.h"

#if SHARED
int io_unlink(const char* path) {
    errno = EIO;
    return -1;
}
weak_alias(io_unlink, __libc_io_unlink);
#else
int __libc_io_unlink(const char* path);
#endif

int unlink(const char* path) {
    return __libc_io_unlink(path);
}
