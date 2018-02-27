#include "threads_impl.h"

int pthread_mutex_lock(pthread_mutex_t* m) {
    if ((m->_m_type & PTHREAD_MUTEX_MASK) == PTHREAD_MUTEX_NORMAL &&
        !a_cas_shim(&m->_m_lock, 0, EBUSY))
        return 0;

    return pthread_mutex_timedlock(m, 0);
}
