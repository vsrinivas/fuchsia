#include "pthread_impl.h"

int pthread_setschedprio(pthread_t t, int prio) {
    int r;
    mtx_lock(&t->killlock);
    r = t->dead ? ESRCH : -__syscall(SYS_sched_setparam, t->tid, &prio);
    mtx_unlock(&t->killlock);
    return r;
}
