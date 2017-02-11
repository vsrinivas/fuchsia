#include "atomic.h"
#include "futex_impl.h"
#include "pthread_impl.h"

static struct {
    atomic_int lock;
    atomic_int waiters;
} vmlock;

void __vm_wait(void) {
    int tmp;
    while ((tmp = atomic_load(&vmlock.lock)))
        __wait(&vmlock.lock, &vmlock.waiters, tmp);
}

void __vm_lock(void) {
    atomic_fetch_add(&vmlock.lock, 1);
}

void __vm_unlock(void) {
    if (atomic_fetch_add(&vmlock.lock, -1) == 1 && atomic_load(&vmlock.waiters))
        __wake(&vmlock.lock, -1);
}
