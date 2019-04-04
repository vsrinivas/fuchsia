#include "futex_impl.h"
#include "threads_impl.h"

int pthread_rwlock_unlock(pthread_rwlock_t* rw) {
    int val, cnt, waiters, new;

    do {
        val = atomic_load(&rw->_rw_lock);
        cnt = val & PTHREAD_MUTEX_RWLOCK_COUNT_MASK;
        waiters = atomic_load(&rw->_rw_waiters);
        new = (cnt == PTHREAD_MUTEX_RWLOCK_LOCKED_FOR_WR || cnt == 1)
            ? PTHREAD_MUTEX_RWLOCK_UNLOCKED
            : val - 1;
    } while (a_cas_shim(&rw->_rw_lock, val, new) != val);

    if (!new && (waiters || (val & PTHREAD_MUTEX_RWLOCK_CONTESTED_BIT)))
        _zx_futex_wake(&rw->_rw_lock, cnt);

    return 0;
}
