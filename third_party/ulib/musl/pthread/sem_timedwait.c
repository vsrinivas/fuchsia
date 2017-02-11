#include "pthread_impl.h"
#include <semaphore.h>

static void cleanup(void* p) {
    a_dec(p);
}

int sem_timedwait(sem_t* restrict sem, const struct timespec* restrict at) {
    pthread_testcancel();

    if (!sem_trywait(sem))
        return 0;

    int spins = 100;
    while (spins-- && sem->_s_value <= 0 && !sem->_s_waiters)
        a_spin();

    while (sem_trywait(sem)) {
        int r;
        a_inc(&sem->_s_waiters);
        a_cas(&sem->_s_value, 0, -1);
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
