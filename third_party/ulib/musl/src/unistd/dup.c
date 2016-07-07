#include <unistd.h>

#include <errno.h>

int dup(int fd) {
    errno = ENOSYS;
    return -1;
}
