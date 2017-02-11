#include "futex_impl.h"
#include "pthread_impl.h"

static int pshared_barrier_wait(pthread_barrier_t* b) {
    int limit = (b->_b_limit & INT_MAX) + 1;
    int ret = 0;
    int v, w;

    if (limit == 1)
        return PTHREAD_BARRIER_SERIAL_THREAD;

    while ((v = a_cas_shim(&b->_b_lock, 0, limit)))
        __wait(&b->_b_lock, &b->_b_waiters, v);

    /* Wait for <limit> threads to get to the barrier */
    if (atomic_fetch_add(&b->_b_count, 1) + 1 == limit) {
        atomic_store(&b->_b_count, 0);
        ret = PTHREAD_BARRIER_SERIAL_THREAD;
        if (atomic_load(&b->_b_waiters2))
            __wake(&b->_b_count, -1);
    } else {
        atomic_store(&b->_b_lock, 0);
        if (atomic_load(&b->_b_waiters))
            __wake(&b->_b_lock, 1);
        while ((v = atomic_load(&b->_b_count)) > 0)
            __wait(&b->_b_count, &b->_b_waiters2, v);
    }

    __vm_lock();

    /* Ensure all threads have a vm lock before proceeding */
    if (atomic_fetch_add(&b->_b_count, -1) == 1 - limit) {
        atomic_store(&b->_b_count, 0);
        if (atomic_load(&b->_b_waiters2))
            __wake(&b->_b_count, -1);
    } else {
        while ((v = atomic_load(&b->_b_count)))
            __wait(&b->_b_count, &b->_b_waiters2, v);
    }

    /* Perform a recursive unlock suitable for self-sync'd destruction */
    do {
        v = atomic_load(&b->_b_lock);
        w = atomic_load(&b->_b_waiters);
    } while (a_cas_shim(&b->_b_lock, v, v == INT_MIN + 1 ? 0 : v - 1) != v);

    /* Wake a thread waiting to reuse or destroy the barrier */
    if (v == INT_MIN + 1 || (v == 1 && w))
        __wake(&b->_b_lock, 1);

    __vm_unlock();

    return ret;
}

struct instance {
    atomic_int count;
    atomic_int last;
    atomic_int waiters;
    atomic_int finished;
};

int pthread_barrier_wait(pthread_barrier_t* b) {
    int limit = b->_b_limit;
    struct instance* inst;

    /* Trivial case: count was set at 1 */
    if (!limit)
        return PTHREAD_BARRIER_SERIAL_THREAD;

    /* Process-shared barriers require a separate, inefficient wait */
    if (limit < 0)
        return pshared_barrier_wait(b);

    /* Otherwise we need a lock on the barrier object */
    while (atomic_exchange(&b->_b_lock, 1))
        __wait(&b->_b_lock, &b->_b_waiters, 1);
    inst = b->_b_inst;

    /* First thread to enter the barrier becomes the "instance owner" */
    if (!inst) {
        struct instance new_inst = {0, 0, 0, ATOMIC_VAR_INIT(0)};
        int spins = 200;
        b->_b_inst = inst = &new_inst;
        atomic_store(&b->_b_lock, 0);
        if (atomic_load(&b->_b_waiters))
            __wake(&b->_b_lock, 1);
        while (spins-- && !atomic_load(&inst->finished))
            a_spin();
        atomic_fetch_add(&inst->finished, 1);
        while (atomic_load(&inst->finished) == 1)
            _mx_futex_wait(&inst->finished, 1, MX_TIME_INFINITE);
        return PTHREAD_BARRIER_SERIAL_THREAD;
    }

    /* Last thread to enter the barrier wakes all non-instance-owners */
    if (atomic_fetch_add(&inst->count, 1) + 1 == limit) {
        b->_b_inst = 0;
        atomic_store(&b->_b_lock, 0);
        if (atomic_load(&b->_b_waiters))
            __wake(&b->_b_lock, 1);
        atomic_store(&inst->last, 1);
        if (inst->waiters)
            __wake(&inst->last, -1);
    } else {
        atomic_store(&b->_b_lock, 0);
        if (atomic_load(&b->_b_waiters))
            __wake(&b->_b_lock, 1);
        __wait(&inst->last, &inst->waiters, 0);
    }

    /* Last thread to exit the barrier wakes the instance owner */
    if (atomic_fetch_add(&inst->count, -1) == 1 && atomic_fetch_add(&inst->finished, 1))
        __wake(&inst->finished, 1);

    return 0;
}
