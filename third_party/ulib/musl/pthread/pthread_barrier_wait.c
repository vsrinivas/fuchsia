#include "futex_impl.h"
#include "threads_impl.h"

struct instance {
    atomic_uint count;
    atomic_int last;
    atomic_int waiters;
    atomic_int finished;
};

int pthread_barrier_wait(pthread_barrier_t* b) {
    unsigned int limit = b->_b_limit;
    struct instance* inst;

    /* Trivial case: count was set at 1 */
    if (limit == 1)
        return PTHREAD_BARRIER_SERIAL_THREAD;

    /* We need a lock on the barrier object */
    while (atomic_exchange(&b->_b_lock, 1))
        __wait(&b->_b_lock, &b->_b_waiters, 1);
    inst = b->_b_inst;

    /* First thread to enter the barrier becomes the "instance owner" */
    if (!inst) {
        struct instance new_inst = {
            ATOMIC_VAR_INIT(0),
            ATOMIC_VAR_INIT(0),
            ATOMIC_VAR_INIT(0),
            ATOMIC_VAR_INIT(0),
        };
        int spins = 200;
        b->_b_inst = inst = &new_inst;
        atomic_store(&b->_b_lock, 0);
        if (atomic_load(&b->_b_waiters))
            __wake(&b->_b_lock, 1);
        while (spins-- && !atomic_load(&inst->finished))
            a_spin();
        atomic_fetch_add(&inst->finished, 1);
        while (atomic_load(&inst->finished) == 1)
            _zx_futex_wait(&inst->finished, 1, ZX_TIME_INFINITE);
        return PTHREAD_BARRIER_SERIAL_THREAD;
    }

    /* Last thread to enter the barrier wakes all
     * non-instance-owners. One is added to the count since we fetch
     * before adding, and another one to account for the fact that the
     * first thread (the "instance owner" above) never gets here. */
    if (atomic_fetch_add(&inst->count, 1) + 2 == limit) {
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
