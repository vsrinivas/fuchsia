#include "threads_impl.h"

int pthread_spin_unlock(pthread_spinlock_t* s) {
    atomic_store(s, 0);
    return 0;
}
