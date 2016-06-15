#include <errno.h>
#include <signal.h>

int killpg(pid_t pgid, int sig) {
    if (pgid < 0) {
        errno = EINVAL;
        return -1;
    }
    return kill(-pgid, sig);
}
