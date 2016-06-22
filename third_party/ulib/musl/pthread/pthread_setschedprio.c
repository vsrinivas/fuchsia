#include "pthread_impl.h"

int pthread_setschedprio(pthread_t t, int prio) {
    int r;
    mxr_mutex_lock(&t->killlock);
    r = t->dead ? ESRCH : -__syscall(SYS_sched_setparam, t->tid, &prio);
    mxr_mutex_unlock(&t->killlock);
    return r;
}
