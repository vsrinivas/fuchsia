#include "futex_impl.h"
#include "pthread_impl.h"

int pthread_barrier_destroy(pthread_barrier_t* b) {
    if (b->_b_limit < 0) {
        if (atomic_load(&b->_b_lock)) {
            int v;
            atomic_fetch_or(&b->_b_lock, INT_MIN);
            while ((v = atomic_load(&b->_b_lock)) & INT_MAX)
                __wait(&b->_b_lock, 0, v);
        }
        __vm_wait();
    }
    return 0;
}
