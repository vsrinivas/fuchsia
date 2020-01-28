#include "atomic.h"
#include "threads_impl.h"

int pthread_rwlock_tryrdlock(pthread_rwlock_t* rw) {
  int val, cnt;
  do {
    val = atomic_load(&rw->_rw_lock);
    cnt = val & PTHREAD_MUTEX_RWLOCK_COUNT_MASK;

    if (cnt == PTHREAD_MUTEX_RWLOCK_LOCKED_FOR_WR) {
      return EBUSY;
    }

    if (cnt == PTHREAD_MUTEX_RWLOCK_MAX_RD_COUNT) {
      return EAGAIN;
    }

  } while (a_cas_shim(&rw->_rw_lock, val, val + 1) != val);
  return 0;
}
