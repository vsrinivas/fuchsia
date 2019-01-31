#include <semaphore.h>

#include <stdatomic.h>

#include "atomic.h"
#include "threads_impl.h"

int sem_timedwait(sem_t* restrict sem, const struct timespec* restrict at) {
    if (!sem_trywait(sem))
        return 0;

    int spins = 100;
    while (spins-- && atomic_load(&sem->_s_value) <= 0 && !atomic_load(&sem->_s_waiters))
        a_spin();

    while (sem_trywait(sem)) {
        atomic_fetch_add(&sem->_s_waiters, 1);
        a_cas_shim(&sem->_s_value, 0, -1);
        int r = __timedwait(&sem->_s_value, -1, CLOCK_REALTIME, at);
        if (r) {
            errno = r;
            return -1;
        }
    }
    return 0;
}
