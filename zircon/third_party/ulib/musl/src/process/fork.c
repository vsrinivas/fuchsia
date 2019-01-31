#include <errno.h>
#include <unistd.h>

pid_t fork(void) {
    errno = ENOSYS;
    return -1;
}
