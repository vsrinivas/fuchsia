#include <errno.h>
#include <signal.h>

int kill(pid_t pid, int sig) {
    errno = EPERM;
    return -1;
}
