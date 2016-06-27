#define _BSD_SOURCE
#include "syscall.h"
#include <sys/uio.h>
#include <unistd.h>

ssize_t preadv(int fd, const struct iovec* iov, int count, off_t ofs) {
    return syscall(SYS_preadv, fd, iov, count, (long)(ofs), (long)(ofs >> 32));
}
