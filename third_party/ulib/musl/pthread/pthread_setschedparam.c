#include "pthread_impl.h"

int pthread_setschedparam(pthread_t t, int policy, const struct sched_param* param) {
    int r;
    mxr_mutex_lock(&t->killlock);
    r = t->dead ? ESRCH : -__syscall(SYS_sched_setscheduler, t->tid, policy, param);
    mxr_mutex_unlock(&t->killlock);
    return r;
}
