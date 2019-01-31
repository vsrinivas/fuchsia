#include "threads_impl.h"

int pthread_rwlock_trywrlock(pthread_rwlock_t* rw) {
    if (a_cas_shim(&rw->_rw_lock, 0, 0x7fffffff))
        return EBUSY;
    return 0;
}
