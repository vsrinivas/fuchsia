#include <errno.h>
#include <fcntl.h>

int fchdir(int fd) {
    errno = ENOSYS;
    return -1;
}
