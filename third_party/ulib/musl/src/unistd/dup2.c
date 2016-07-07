#include <unistd.h>

#include <errno.h>

int dup2(int old, int new) {
    errno = ENOSYS;
    return -1;
}
