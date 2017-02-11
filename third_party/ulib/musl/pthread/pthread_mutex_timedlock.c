#include "pthread_impl.h"

int pthread_mutex_timedlock(pthread_mutex_t* restrict m, const struct timespec* restrict at) {
    if ((m->_m_type & 15) == PTHREAD_MUTEX_NORMAL && !a_cas_shim(&m->_m_lock, 0, EBUSY))
        return 0;

    int r, t;

    r = pthread_mutex_trylock(m);
    if (r != EBUSY)
        return r;

    int spins = 100;
    while (spins-- && atomic_load(&m->_m_lock) && !atomic_load(&m->_m_waiters))
        a_spin();

    while ((r = pthread_mutex_trylock(m)) == EBUSY) {
        if (!(r = atomic_load(&m->_m_lock)) || ((r & 0x40000000) && (m->_m_type & 4)))
            continue;
        if ((m->_m_type & 3) == PTHREAD_MUTEX_ERRORCHECK &&
            (r & 0x7fffffff) == __thread_get_tid())
            return EDEADLK;

        atomic_fetch_add(&m->_m_waiters, 1);
        t = r | 0x80000000;
        a_cas_shim(&m->_m_lock, r, t);
        r = __timedwait(&m->_m_lock, t, CLOCK_REALTIME, at);
        atomic_fetch_sub(&m->_m_waiters, 1);
        if (r)
            break;
    }
    return r;
}
