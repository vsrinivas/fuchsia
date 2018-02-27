#include "threads_impl.h"

int pthread_spin_init(pthread_spinlock_t* s, int shared) {
    atomic_store(s, 0);
    return 0;
}
