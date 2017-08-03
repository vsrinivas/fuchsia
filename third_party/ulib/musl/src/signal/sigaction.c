#include <errno.h>
#include <signal.h>

int sigaction(int sig, const struct sigaction* restrict sa, struct sigaction* restrict old) {
    errno = ENOSYS;
    return -1;
}
