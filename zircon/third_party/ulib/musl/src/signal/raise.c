#include <signal.h>

#include <errno.h>

int raise(int sig) {
    errno = ENOSYS;
    return -1;
}
