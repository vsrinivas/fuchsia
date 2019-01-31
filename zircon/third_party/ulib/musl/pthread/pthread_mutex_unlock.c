#include "threads_impl.h"

#include "futex_impl.h"

int pthread_mutex_unlock(pthread_mutex_t* m) {
    int waiters = atomic_load(&m->_m_waiters);
    int cont;
    int type = m->_m_type & PTHREAD_MUTEX_MASK;

    if (type != PTHREAD_MUTEX_NORMAL) {
        if ((atomic_load(&m->_m_lock) & PTHREAD_MUTEX_OWNED_LOCK_MASK) != __thread_get_tid())
            return EPERM;
        if ((type & PTHREAD_MUTEX_MASK) == PTHREAD_MUTEX_RECURSIVE && m->_m_count)
            return m->_m_count--, 0;
    }

    // Release the mutex.
    cont = atomic_exchange(&m->_m_lock, 0);

    // At this point, the mutex was unlocked.  In some usage patterns
    // (e.g. for reference counting), another thread might now acquire the
    // mutex and free the memory containing it.  This means we must not
    // dereference |m| from this point onwards.

    if (waiters || cont < 0) {
        // Note that the mutex's memory could have been freed and reused by
        // this point, so this could cause a spurious futex wakeup for a
        // unrelated user of the memory location.
        _zx_futex_wake(&m->_m_lock, 1);
    }
    return 0;
}
