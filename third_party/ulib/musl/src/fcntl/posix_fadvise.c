#include "libc.h"
#include "syscall.h"
#include <fcntl.h>

int posix_fadvise(int fd, off_t base, off_t len, int advice) {
    return -(__syscall(SYS_fadvise, fd, base, len, advice));
}

LFS64(posix_fadvise);
