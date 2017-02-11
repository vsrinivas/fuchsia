#include "pthread_impl.h"
#include <semaphore.h>

static void cleanup(void* p) {
    atomic_int* waiters = p;
    atomic_fetch_sub(waiters, 1);
}

int sem_timedwait(sem_t* restrict sem, const struct timespec* restrict at) {
    pthread_testcancel();

    if (!sem_trywait(sem))
        return 0;

    int spins = 100;
    while (spins-- && atomic_load(&sem->_s_value) <= 0 && !atomic_load(&sem->_s_waiters))
        a_spin();

    while (sem_trywait(sem)) {
        int r;
        atomic_fetch_add(&sem->_s_waiters, 1);
        a_cas_shim(&sem->_s_value, 0, -1);
        pthread_cleanup_push(cleanup, (void*)(&sem->_s_waiters));
        r = __timedwait_cp(&sem->_s_value, -1, CLOCK_REALTIME, at);
        pthread_cleanup_pop(1);
        if (r) {
            errno = r;
            return -1;
        }
    }
    return 0;
}
