#include "threads_impl.h"
#include <semaphore.h>

int sem_trywait(sem_t* sem) {
    int val;
    while ((val = atomic_load(&sem->_s_value)) > 0) {
        int new = val - 1 - (val == 1 && atomic_load(&sem->_s_waiters));
        if (a_cas_shim(&sem->_s_value, val, new) == val)
            return 0;
    }
    errno = EAGAIN;
    return -1;
}
