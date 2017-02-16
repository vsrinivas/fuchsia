#include "atomic.h"
#include "futex_impl.h"
#include "pthread_impl.h"

static volatile int vmlock[2];

void __vm_wait(void) {
    int tmp;
    while ((tmp = vmlock[0]))
        __wait(vmlock, vmlock + 1, tmp);
}

void __vm_lock(void) {
    a_inc(vmlock);
}

void __vm_unlock(void) {
    if (a_fetch_add(vmlock, -1) == 1 && vmlock[1])
        __wake(vmlock, -1);
}
