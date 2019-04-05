#include "threads_impl.h"

int pthread_rwlock_timedwrlock(pthread_rwlock_t* restrict rw, const struct timespec* restrict at) {
    int r, t;

    r = pthread_rwlock_trywrlock(rw);
    if (r != EBUSY)
        return r;

    int spins = 100;
    while (spins-- && atomic_load(&rw->_rw_lock) && !atomic_load(&rw->_rw_waiters))
        a_spin();

    while ((r = pthread_rwlock_trywrlock(rw)) == EBUSY) {
        if (!(r = atomic_load(&rw->_rw_lock)))
            continue;
        t = r | PTHREAD_MUTEX_OWNED_LOCK_BIT;
        atomic_fetch_add(&rw->_rw_waiters, 1);
        a_cas_shim(&rw->_rw_lock, r, t);
        r = __timedwait(&rw->_rw_lock, t, CLOCK_REALTIME, at);
        atomic_fetch_sub(&rw->_rw_waiters, 1);
        if (r)
            return r;
    }
    return r;
}
