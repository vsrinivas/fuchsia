#include "syscall.h"
#include <fcntl.h>
#include <stdarg.h>

#if SHARED
static int io_open(const char* filename, int flags, int mode) {
    return -1;
}
weak_alias(io_open, __libc_io_open);
#else
int __libc_io_open(const char* filename, int flags, int mode);
#endif

int open(const char* filename, int flags, ...) {
    mode_t mode = 0;

    if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
    }

    int fd = __libc_io_open(filename, flags, mode);
    // TODO(kulakowski) Implement O_CLOEXEC semantics
    // if (fd >= 0 && (flags & O_CLOEXEC)) __syscall(SYS_fcntl, fd, F_SETFD, FD_CLOEXEC);

    return __syscall_ret(fd);
}
