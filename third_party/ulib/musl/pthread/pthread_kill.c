#include "pthread_impl.h"

int pthread_kill(pthread_t t, int sig) {
    int r;
    mxr_mutex_lock(&t->killlock);
    r = t->dead ? ESRCH : -__syscall(SYS_tkill, t->tid, sig);
    mxr_mutex_unlock(&t->killlock);
    return r;
}
