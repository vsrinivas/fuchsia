#include "threads_impl.h"
#include <errno.h>

int pthread_spin_lock(pthread_spinlock_t* s) {
    while (atomic_load(s) || a_cas_shim(s, 0, EBUSY))
        a_spin();
    return 0;
}
