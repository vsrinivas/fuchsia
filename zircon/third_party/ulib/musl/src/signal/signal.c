#include "libc.h"
#include <errno.h>
#include <signal.h>

void (*signal(int sig, void (*func)(int)))(int) {
    errno = ENOSYS;
    return SIG_ERR;
}

weak_alias(signal, bsd_signal);
weak_alias(signal, __sysv_signal);
