#include "libc.h"
#include "syscall.h"
#include <errno.h>
#include <unistd.h>

#if WITH_LIB_SO
static int io_close(int fd) {
    return 0;
}
weak_alias(io_close, __libc_io_close);
#else
int __libc_io_close(int fd);
#endif

int close(int fd) {
    return __libc_io_close(fd);
}
