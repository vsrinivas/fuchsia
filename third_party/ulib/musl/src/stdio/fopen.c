#include "stdio_impl.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

FILE* fopen(const char* restrict filename, const char* restrict mode) {
    FILE* f;
    int fd;
    int flags;

    /* Check for valid initial mode character */
    if (!strchr("rwa", *mode)) {
        errno = EINVAL;
        return 0;
    }

    /* Compute the flags to pass to open() */
    flags = __fmodeflags(mode);

    fd = open(filename, flags, 0666);
    if (fd < 0)
        return 0;
    // TODO(kulakowski) Implement O_CLOEXEC semantics.
    if (flags & O_CLOEXEC)
        __syscall(SYS_fcntl, fd, F_SETFD, FD_CLOEXEC);

    f = __fdopen(fd, mode);
    if (f)
        return f;

    close(fd);
    return 0;
}
