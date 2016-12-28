#include "pthread_impl.h"

int pthread_kill(pthread_t t, int sig) {
    int r;
    mtx_lock(&t->killlock);
    // TODO(kulakowski) Signals.
    r = t->dead ? ESRCH : ENOSYS;
    mtx_unlock(&t->killlock);
    return r;
}
