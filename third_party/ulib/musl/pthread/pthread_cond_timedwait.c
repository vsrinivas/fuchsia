#include "futex_impl.h"
#include "pthread_impl.h"

/*
 * struct waiter
 *
 * Waiter objects have automatic storage on the waiting thread, and
 * are used in building a linked list representing waiters currently
 * waiting on the condition variable or a group of waiters woken
 * together by a broadcast or signal; in the case of signal, this is a
 * degenerate list of one member.
 *
 * Waiter lists attached to the condition variable itself are
 * protected by the lock on the cv. Detached waiter lists are never
 * modified again, but can only be traversed in reverse order, and are
 * protected by the "barrier" locks in each node, which are unlocked
 * in turn to control wake order.
 */

struct waiter {
    struct waiter *prev, *next;
    atomic_int state;
    atomic_int barrier;
    atomic_int* notify;
};

enum {
    WAITING,
    SIGNALED,
    LEAVING,
};

int pthread_cond_timedwait(pthread_cond_t* restrict c, pthread_mutex_t* restrict m,
                           const struct timespec* restrict ts) {
    int e, clock = c->_c_clock, oldstate, tmp;

    if ((m->_m_type != PTHREAD_MUTEX_NORMAL) &&
        (m->_m_lock & PTHREAD_MUTEX_OWNED_LOCK_MASK) != __thread_get_tid())
        return EPERM;

    if (ts && ts->tv_nsec >= 1000000000UL)
        return EINVAL;

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

    pthread_mutex_unlock(m);

    /* Wait to be signaled.  There are multiple ways this loop could exit:
     *  1) After being woken by __private_cond_signal().
     *  2) After being woken by pthread_mutex_unlock(), after we were
     *     requeued from the condvar's futex to the mutex's futex (by
     *     pthread_cond_timedwait() in another thread).
     *  3) After a timeout.
     *  4) On Linux, interrupted by an asynchronous signal.  This does
     *     not apply on Magenta. */
    do
        e = __timedwait(fut, seq, clock, ts);
    while (*fut == seq && !e);

    oldstate = a_cas_shim(&node.state, WAITING, LEAVING);

    if (oldstate == WAITING) {
        /* The wait timed out.  So far, this thread was not signaled
         * by pthread_cond_signal()/broadcast() -- this thread was
         * able to move state.node out of the WAITING state before any
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
        /* This thread was at least partially signaled by
         * pthread_cond_signal()/broadcast().  That might have raced
         * with a timeout, so we need to wait for this thread to be
         * fully signaled.  We need to wait until another thread sets
         * node.barrier to 0.  (This lock() call will also set
         * node.barrier to non-zero, but that side effect is
         * unnecessary here.) */
        lock(&node.barrier);
    }

    /* Errors locking the mutex override any existing error, since the
     * caller must see them to know the state of the mutex. */
    if ((tmp = pthread_mutex_lock(m)))
        e = tmp;

    if (oldstate == WAITING)
        goto done;

    /* By this point, our part of the waiter list cannot change further.
     * It has been unlinked from the condvar by __private_cond_signal().
     * It consists only of waiters that were woken explicitly by
     * pthread_cond_signal()/broadcast().  Any timed-out waiters would have
     * removed themselves from the list before __private_cond_signal()
     * signaled the first node.barrier in our list.
     *
     * It is therefore safe now to read node.next and node.prev without
     * holding _c_lock. */

    /* As an optimization, we only update _m_waiters at the beginning and
     * end of the woken list. */
    if (!node.next)
        atomic_fetch_add(&m->_m_waiters, 1);

    /* Unlock the barrier that's holding back the next waiter, and
     * either wake it or requeue it to the mutex. */
    if (node.prev)
        unlock_requeue(&node.prev->barrier, &m->_m_lock);
    else
        atomic_fetch_sub(&m->_m_waiters, 1);

done:

    return e;
}

/* This will wake upto |n| threads that are waiting on the condvar.  This
 * is used to implement pthread_cond_signal() (for n=1) and
 * pthread_cond_broadcast() (for n=-1). */
void __private_cond_signal(void* condvar, int n) {
    pthread_cond_t* c = condvar;
    struct waiter *p, *first = 0;
    atomic_int ref = ATOMIC_VAR_INIT(0);
    int cur;

    lock(&c->_c_lock);
    for (p = c->_c_tail; n && p; p = p->prev) {
        if (a_cas_shim(&p->state, WAITING, SIGNALED) != WAITING) {
            /* This waiter timed out, and it marked itself as in the
             * LEAVING state.  However, it hasn't yet claimed _c_lock
             * (since we claimed the lock first) and so it hasn't yet
             * removed itself from the list.  We will wait for the waiter
             * to remove itself from the list and to notify us of that. */
            atomic_fetch_add(&ref, 1);
            p->notify = &ref;
        } else {
            n--;
            if (!first)
                first = p;
        }
    }
    /* Split the list, leaving any remainder on the cv. */
    if (p) {
        if (p->next)
            p->next->prev = 0;
        p->next = 0;
    } else {
        c->_c_head = 0;
    }
    c->_c_tail = p;
    unlock(&c->_c_lock);

    /* Wait for any waiters in the LEAVING state to remove
     * themselves from the list before returning or allowing
     * signaled threads to proceed. */
    while ((cur = atomic_load(&ref)))
        __wait(&ref, 0, cur);

    /* Allow first signaled waiter, if any, to proceed. */
    if (first)
        unlock(&first->barrier);
}
