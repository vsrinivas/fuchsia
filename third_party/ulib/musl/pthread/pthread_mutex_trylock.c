#include "threads_impl.h"

int __pthread_mutex_trylock_owner(pthread_mutex_t* m) {
    int old, own;
    int type = m->_m_type & PTHREAD_MUTEX_MASK;
    int tid = __thread_get_tid();

    old = atomic_load(&m->_m_lock);
    own = old & PTHREAD_MUTEX_OWNED_LOCK_MASK;
    if (own == tid && (type & PTHREAD_MUTEX_MASK) == PTHREAD_MUTEX_RECURSIVE) {
        if ((unsigned)m->_m_count >= INT_MAX)
            return EAGAIN;
        m->_m_count++;
        return 0;
    }

    if (own || a_cas_shim(&m->_m_lock, old, tid) != old) {
        return EBUSY;
    }

    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t* m) {
    if ((m->_m_type & PTHREAD_MUTEX_MASK) == PTHREAD_MUTEX_NORMAL)
        return a_cas_shim(&m->_m_lock, 0, EBUSY) & EBUSY;
    return __pthread_mutex_trylock_owner(m);
}
