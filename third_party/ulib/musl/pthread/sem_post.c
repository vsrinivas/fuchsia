#include "futex_impl.h"
#include "threads_impl.h"
#include <semaphore.h>

int sem_post(sem_t* sem) {
    int val, waiters;
    do {
        val = atomic_load(&sem->_s_value);
        waiters = atomic_load(&sem->_s_waiters);
        if (val == SEM_VALUE_MAX) {
            errno = EOVERFLOW;
            return -1;
        }
    } while (a_cas_shim(&sem->_s_value, val, val + 1 + (val < 0)) != val);
    if (val < 0 || waiters)
        __wake(&sem->_s_value, 1);
    return 0;
}
