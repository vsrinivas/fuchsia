#include <threads.h>

#include <errno.h>

#include <runtime/mutex.h>

#include "futex_impl.h"
#include "libc.h"
#include "pthread_impl.h"

struct waiter {
    struct waiter *prev, *next;
    atomic_int state;
    atomic_int barrier;
    atomic_int* notify;
};

enum {
    WAITING,
    LEAVING,
};

int cnd_timedwait(cnd_t* restrict c, mtx_t* restrict mutex,
                  const struct timespec* restrict ts) {
    mxr_mutex_t* m = (mxr_mutex_t*)mutex;
    int e, clock = c->_c_clock, oldstate;

    if (ts && ts->tv_nsec >= 1000000000UL)
        return thrd_error;

    lock(&c->_c_lock);

    int seq = 2;
    struct waiter node = {
        .barrier = ATOMIC_VAR_INIT(seq),
        .state = ATOMIC_VAR_INIT(WAITING),
    };
    atomic_int* fut = &node.barrier;
    /* Add our waiter node onto the condvar's list.  We add the node to the
     * head of the list, but this is logically the end of the queue. */
    node.next = c->_c_head;
    c->_c_head = &node;
    if (!c->_c_tail)
        c->_c_tail = &node;
    else
        node.next->prev = &node;

    unlock(&c->_c_lock);

    mxr_mutex_unlock(m);

    /* Wait to be signaled.  There are multiple ways this loop could exit:
     *  1) After being woken by __private_cond_signal().
     *  2) After being woken by mxr_mutex_unlock(), after we were
     *     requeued from the condvar's futex to the mutex's futex (by
     *     cnd_timedwait() in another thread).
     *  3) After a timeout.
     *  4) On Linux, interrupted by an asynchronous signal.  This does
     *     not apply on Magenta. */
    do
        e = __timedwait(fut, seq, clock, ts);
    while (*fut == seq && !e);

    oldstate = a_cas_shim(&node.state, WAITING, LEAVING);

    if (oldstate == WAITING) {
        /* The wait timed out.  So far, this thread was not signaled by
         * cnd_signal()/cnd_broadcast() -- this thread was able to move
         * state.node out of the WAITING state before any
         * __private_cond_signal() call could do that.
         *
         * This thread must therefore remove the waiter node from the
         * list itself. */

        /* Access to cv object is valid because this waiter was not
         * yet signaled and a new signal/broadcast cannot return
         * after seeing a LEAVING waiter without getting notified
         * via the futex notify below. */

        lock(&c->_c_lock);

        /* Remove our waiter node from the list. */
        if (c->_c_head == &node)
            c->_c_head = node.next;
        else if (node.prev)
            node.prev->next = node.next;
        if (c->_c_tail == &node)
            c->_c_tail = node.prev;
        else if (node.next)
            node.next->prev = node.prev;

        unlock(&c->_c_lock);

        /* It is possible that __private_cond_signal() saw our waiter node
         * after we set node.state to LEAVING but before we removed the
         * node from the list.  If so, it will have set node.notify and
         * will be waiting on it, and we need to wake it up.
         *
         * This is rather complex.  An alternative would be to eliminate
         * the node.state field and always claim _c_lock if we could have
         * got a timeout.  However, that presumably has higher overhead
         * (since it contends _c_lock and involves more atomic ops). */
        if (node.notify) {
            if (atomic_fetch_add(node.notify, -1) == 1)
                __wake(node.notify, 1);
        }
    } else {
        /* Lock barrier first to control wake order. */
        lock(&node.barrier);
    }

    /* We must leave the mutex in the "locked with waiters" state here.
     * There are two reasons for that:
     *  1) If we do the unlock_requeue() below, a condvar waiter will be
     *     requeued to the mutex's futex.  We need to ensure that it will
     *     be signaled by mxr_mutex_unlock() in future.
     *  2) If the current thread was woken via an unlock_requeue() +
     *     mxr_mutex_unlock(), there *might* be another thread waiting for
     *     the mutex after us in the queue.  We need to ensure that it
     *     will be signaled by mxr_mutex_unlock() in future. */
    mxr_mutex_lock_with_waiter(m);

    /* By this point, our part of the waiter list cannot change further.
     * It has been unlinked from the condvar by __private_cond_signal().
     * It consists only of waiters that were woken explicitly by
     * cnd_signal()/cnd_broadcast().  Any timed-out waiters would have
     * removed themselves from the list before __private_cond_signal()
     * signaled the first node.barrier in our list.
     *
     * It is therefore safe now to read node.next and node.prev without
     * holding _c_lock. */

    if (oldstate != WAITING && node.prev) {
        /* Unlock the barrier that's holding back the next waiter, and
         * requeue it to the mutex so that it will be woken when the
         * mutex is unlocked. */
        unlock_requeue(&node.prev->barrier, &m->futex);
    }

    switch (e) {
    case 0:
        return 0;
    case EINVAL:
        return thrd_error;
    case ETIMEDOUT:
        return thrd_timedout;
    default:
        // No other error values are permissible from __timedwait_cp();
        __builtin_trap();
    }
}
