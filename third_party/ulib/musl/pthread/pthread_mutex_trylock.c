#include "pthread_impl.h"

int __pthread_mutex_trylock_owner(pthread_mutex_t* m) {
    int old, own;
    int type = m->_m_type & 15;
    int tid = __thread_get_tid();

    old = m->_m_lock;
    own = old & 0x7fffffff;
    if (own == tid && (type & 3) == PTHREAD_MUTEX_RECURSIVE) {
        if ((unsigned)m->_m_count >= INT_MAX)
            return EAGAIN;
        m->_m_count++;
        return 0;
    }
    if (own == 0x40000000)
        return ENOTRECOVERABLE;

    if ((own && (!(own & 0x40000000) || !(type & 4))) || a_cas(&m->_m_lock, old, tid) != old) {
        return EBUSY;
    }

    if (own) {
        m->_m_count = 0;
        m->_m_type |= 8;
        return EOWNERDEAD;
    }

    return 0;
}

int __pthread_mutex_trylock(pthread_mutex_t* m) {
    if ((m->_m_type & 15) == PTHREAD_MUTEX_NORMAL)
        return a_cas(&m->_m_lock, 0, EBUSY) & EBUSY;
    return __pthread_mutex_trylock_owner(m);
}

weak_alias(__pthread_mutex_trylock, pthread_mutex_trylock);
