#include <threads.h>

#include <errno.h>

#include <runtime/mutex.h>

#include "futex_impl.h"
#include "libc.h"
#include "pthread_impl.h"

struct waiter {
    struct waiter *prev, *next;
    volatile int state, barrier;
    volatile int* notify;
};

/* Self-synchronized-destruction-safe lock functions */

static inline void lock(volatile int* l) {
    if (a_cas(l, 0, 1)) {
        a_cas(l, 1, 2);
        do
            __wait(l, 0, 2);
        while (a_cas(l, 0, 2));
    }
}

static inline void unlock(volatile int* l) {
    if (a_swap(l, 0) == 2)
        __wake(l, 1);
}

static inline void unlock_requeue(volatile int* l, mx_futex_t* r) {
    a_store(l, 0);
    _mx_futex_requeue((void*)l, /* wake count */ 0, /* l futex value */ 0,
                      r, /* requeue count */ 1);
}

enum {
    WAITING,
    LEAVING,
};

int cnd_timedwait(cnd_t* restrict c, mtx_t* restrict mutex,
                  const struct timespec* restrict ts) {
    mxr_mutex_t* m = (mxr_mutex_t*)mutex;
    struct waiter node = {0};
    int e, seq, clock = c->_c_clock, oldstate;
    volatile int* fut;

    if (ts && ts->tv_nsec >= 1000000000UL)
        return thrd_error;

    lock(&c->_c_lock);

    seq = node.barrier = 2;
    fut = &node.barrier;
    node.state = WAITING;
    node.next = c->_c_head;
    c->_c_head = &node;
    if (!c->_c_tail)
        c->_c_tail = &node;
    else
        node.next->prev = &node;

    unlock(&c->_c_lock);

    mxr_mutex_unlock(m);

    do
        e = __timedwait_cp(fut, seq, clock, ts);
    while (*fut == seq && !e);

    oldstate = a_cas(&node.state, WAITING, LEAVING);

    if (oldstate == WAITING) {
        /* Access to cv object is valid because this waiter was not
         * yet signaled and a new signal/broadcast cannot return
         * after seeing a LEAVING waiter without getting notified
         * via the futex notify below. */

        lock(&c->_c_lock);

        if (c->_c_head == &node)
            c->_c_head = node.next;
        else if (node.prev)
            node.prev->next = node.next;
        if (c->_c_tail == &node)
            c->_c_tail = node.prev;
        else if (node.next)
            node.next->prev = node.prev;

        unlock(&c->_c_lock);

        if (node.notify) {
            if (a_fetch_add(node.notify, -1) == 1)
                __wake(node.notify, 1);
        }
    } else {
        /* Lock barrier first to control wake order. */
        lock(&node.barrier);
    }

    mxr_mutex_lock(m);

    if (oldstate != WAITING) {
        // TODO(kulakowski) If mxr_mutex_t grows a waiters count, increment it here.
        // if (!node.next)
        //     a_inc(&mutex->_m_waiters);

        /* Unlock the barrier that's holding back the next waiter, and
         * either wake it or requeue it to the mutex. */
        if (node.prev)
            unlock_requeue(&node.prev->barrier, &m->futex);
        // TODO(kulakowski) If mxr_mutex_t grows a waiters count, decrement it here.
        // else
        //     a_dec(&mutex->_m_waiters);
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
