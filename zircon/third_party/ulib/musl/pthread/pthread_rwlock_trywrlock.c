#include "atomic.h"
#include "threads_impl.h"

int pthread_rwlock_trywrlock(pthread_rwlock_t* rw) {
  if (a_cas_shim(&rw->_rw_lock, PTHREAD_MUTEX_RWLOCK_UNLOCKED,
                 PTHREAD_MUTEX_RWLOCK_LOCKED_FOR_WR)) {
    return EBUSY;
  }
  return 0;
}
