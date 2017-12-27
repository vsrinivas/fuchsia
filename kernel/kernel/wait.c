// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <kernel/wait.h>

#include <err.h>
#include <kernel/sched.h>
#include <kernel/thread.h>
#include <kernel/timer.h>
#include <lib/ktrace.h>
#include <platform.h>

void wait_queue_init(wait_queue_t* wait) {
    *wait = (wait_queue_t)WAIT_QUEUE_INITIAL_VALUE(*wait);
}

// Disable thread safety analysis here since Clang has trouble with the analysis
// around timer_trylock_or_cancel.
static enum handler_return wait_queue_timeout_handler(timer_t* timer, zx_time_t now,
                                                      void* arg) TA_NO_THREAD_SAFETY_ANALYSIS {
    thread_t* thread = (thread_t*)arg;

    DEBUG_ASSERT(thread->magic == THREAD_MAGIC);

    /* spin trylocking on the thread lock since the routine that set up the callback,
     * wait_queue_block, may be trying to simultaneously cancel this timer while holding the
     * thread_lock.
     */
    if (timer_trylock_or_cancel(timer, &thread_lock))
        return INT_NO_RESCHEDULE;

    bool local_resched;
    enum handler_return ret = INT_NO_RESCHEDULE;
    if (wait_queue_unblock_thread(thread, ZX_ERR_TIMED_OUT, &local_resched) >= ZX_OK) {
        ret = local_resched ? INT_RESCHEDULE : INT_NO_RESCHEDULE;
    }

    spin_unlock(&thread_lock);

    return ret;
}

static zx_status_t wait_queue_block_worker(wait_queue_t* wait, zx_time_t deadline,
                                           uint signal_mask) {
    timer_t timer;

    thread_t* current_thread = get_current_thread();

    DEBUG_ASSERT(wait->magic == WAIT_QUEUE_MAGIC);
    DEBUG_ASSERT(current_thread->state == THREAD_RUNNING);
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    if (deadline != ZX_TIME_INFINITE && deadline <= current_time()) {
        return ZX_ERR_TIMED_OUT;
    }

    if (current_thread->interruptable &&
        (unlikely(current_thread->signals & ~signal_mask))) {
        if (current_thread->signals & THREAD_SIGNAL_KILL) {
            return ZX_ERR_INTERNAL_INTR_KILLED;
        } else if (current_thread->signals & THREAD_SIGNAL_SUSPEND) {
            return ZX_ERR_INTERNAL_INTR_RETRY;
        }
    }

    list_add_tail(&wait->list, &current_thread->queue_node);
    wait->count++;
    current_thread->state = THREAD_BLOCKED;
    current_thread->blocking_wait_queue = wait;
    current_thread->blocked_status = ZX_OK;

    /* if the deadline is nonzero or noninfinite, set a callback to yank us out of the queue */
    if (deadline != ZX_TIME_INFINITE) {
        timer_init(&timer);
        timer_set_oneshot(&timer, deadline, wait_queue_timeout_handler, (void*)current_thread);
    }

    ktrace(TAG_KWAIT_BLOCK, (uintptr_t)wait >> 32, (uintptr_t)wait, 0, 0);

    sched_block();

    ktrace(TAG_KWAIT_UNBLOCK, (uintptr_t)wait >> 32, (uintptr_t)wait, current_thread->blocked_status, 0);

    /* we don't really know if the timer fired or not, so it's better safe to try to cancel it */
    if (deadline != ZX_TIME_INFINITE) {
        timer_cancel(&timer);
    }

    return current_thread->blocked_status;
}

/**
 * @brief  Block until a wait queue is notified.
 *
 * This function puts the current thread at the end of a wait
 * queue and then blocks until some other thread wakes the queue
 * up again.
 *
 * @param  wait     The wait queue to enter
 * @param  deadline The time at which to abort the wait
 *
 * If the deadline is zero, this function returns immediately with
 * ZX_ERR_TIMED_OUT.  If the deadline is ZX_TIME_INFINITE, this function
 * waits indefinitely.  Otherwise, this function returns with
 * ZX_ERR_TIMED_OUT when the deadline occurs.
 *
 * @return ZX_ERR_TIMED_OUT on timeout, else returns the return
 * value specified when the queue was woken by wait_queue_wake_one().
 */
zx_status_t wait_queue_block(wait_queue_t* wait, zx_time_t deadline) {
    return wait_queue_block_worker(wait, deadline, 0);
}

/**
 * @brief  Block until a wait queue is notified, ignoring existing signals
 *         in |signal_mask|.
 *
 * This function puts the current thread at the end of a wait
 * queue and then blocks until some other thread wakes the queue
 * up again.
 *
 * @param  wait        The wait queue to enter
 * @param  deadline    The time at which to abort the wait
 * @param  signal_mask Mask of existing signals to ignore
 *
 * If the deadline is zero, this function returns immediately with
 * ZX_ERR_TIMED_OUT.  If the deadline is ZX_TIME_INFINITE, this function
 * waits indefinitely.  Otherwise, this function returns with
 * ZX_ERR_TIMED_OUT when the deadline occurs.
 *
 * @return ZX_ERR_TIMED_OUT on timeout, else returns the return
 * value specified when the queue was woken by wait_queue_wake_one().
 */
zx_status_t wait_queue_block_with_mask(wait_queue_t* wait, zx_time_t deadline,
                                       uint signal_mask) {
    return wait_queue_block_worker(wait, deadline, signal_mask);
}

/**
 * @brief  Wake up one thread sleeping on a wait queue
 *
 * This function removes one thread (if any) from the head of the wait queue and
 * makes it executable.  The new thread will be placed at the head of the
 * run queue.
 *
 * @param wait  The wait queue to wake
 * @param reschedule  If true, the newly-woken thread will run immediately.
 * @param wait_queue_error  The return value which the new thread will receive
 * from wait_queue_block().
 *
 * @return  The number of threads woken (zero or one)
 */
int wait_queue_wake_one(wait_queue_t* wait, bool reschedule, zx_status_t wait_queue_error) {
    thread_t* t;
    int ret = 0;

    DEBUG_ASSERT(wait->magic == WAIT_QUEUE_MAGIC);
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    t = list_remove_head_type(&wait->list, thread_t, queue_node);
    if (t) {
        wait->count--;
        DEBUG_ASSERT(t->state == THREAD_BLOCKED);
        t->blocked_status = wait_queue_error;
        t->blocking_wait_queue = NULL;

        ktrace(TAG_KWAIT_WAKE, (uintptr_t)wait >> 32, (uintptr_t)wait, 0, 0);

        /* wake up the new thread, putting it in a run queue on a cpu. reschedule if the local */
        /* cpu run queue was modified */
        bool local_resched = sched_unblock(t);
        if (reschedule && local_resched)
            sched_reschedule();

        ret = 1;
    }

    return ret;
}

thread_t* wait_queue_dequeue_one(wait_queue_t* wait, zx_status_t wait_queue_error) {
    thread_t* t;

    DEBUG_ASSERT(wait->magic == WAIT_QUEUE_MAGIC);
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    t = list_remove_head_type(&wait->list, thread_t, queue_node);
    if (t) {
        wait->count--;
        DEBUG_ASSERT(t->state == THREAD_BLOCKED);
        t->blocked_status = wait_queue_error;
        t->blocking_wait_queue = NULL;
    }

    return t;
}

/**
 * @brief  Wake all threads sleeping on a wait queue
 *
 * This function removes all threads (if any) from the wait queue and
 * makes them executable.  The new threads will be placed at the head of the
 * run queue.
 *
 * @param wait  The wait queue to wake
 * @param reschedule  If true, the newly-woken threads will run immediately.
 * @param wait_queue_error  The return value which the new thread will receive
 * from wait_queue_block().
 *
 * @return  The number of threads woken
 */
int wait_queue_wake_all(wait_queue_t* wait, bool reschedule, zx_status_t wait_queue_error) {
    thread_t* t;
    int ret = 0;

    DEBUG_ASSERT(wait->magic == WAIT_QUEUE_MAGIC);
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    if (wait->count == 0)
        return 0;

    struct list_node list = LIST_INITIAL_VALUE(list);

    /* pop all the threads off the wait queue into the run queue */
    while ((t = list_remove_head_type(&wait->list, thread_t, queue_node))) {
        wait->count--;

        DEBUG_ASSERT(t->state == THREAD_BLOCKED);
        t->blocked_status = wait_queue_error;
        t->blocking_wait_queue = NULL;

        list_add_tail(&list, &t->queue_node);

        ret++;
    }

    DEBUG_ASSERT(ret > 0);
    DEBUG_ASSERT(wait->count == 0);

    ktrace(TAG_KWAIT_WAKE, (uintptr_t)wait >> 32, (uintptr_t)wait, 0, 0);

    /* wake up the new thread(s), putting it in a run queue on a cpu. reschedule if the local */
    /* cpu run queue was modified */
    bool local_resched = sched_unblock_list(&list);
    if (reschedule && local_resched)
        sched_reschedule();

    return ret;
}

bool wait_queue_is_empty(wait_queue_t* wait) {
    DEBUG_ASSERT(wait->magic == WAIT_QUEUE_MAGIC);
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    return list_is_empty(&wait->list);
}

/**
 * @brief  Tear down a wait queue
 *
 * This panics if any threads were waiting on this queue, because that
 * would indicate a race condition for most uses of wait queues.  If a
 * thread is currently waiting, it could have been scheduled later, in
 * which case it would have called wait_queue_block() on an invalid wait
 * queue.
 */
void wait_queue_destroy(wait_queue_t* wait) {
    DEBUG_ASSERT(wait->magic == WAIT_QUEUE_MAGIC);

    if (!list_is_empty(&wait->list)) {
        panic("wait_queue_destroy() called on non-empty wait_queue_t\n");
    }

    wait->magic = 0;
}

/**
 * @brief  Wake a specific thread in a wait queue
 *
 * This function extracts a specific thread from a wait queue, wakes it, and
 * puts it at the head of the run queue.
 *
 * @param t  The thread to wake
 * @param wait_queue_error  The return value which the new thread will receive from wait_queue_block().
 * @param local_resched  Returns if the caller should reschedule locally.
 *
 * @return ZX_ERR_BAD_STATE if thread was not in any wait queue.
 */
zx_status_t wait_queue_unblock_thread(thread_t* t, zx_status_t wait_queue_error, bool* local_resched) {
    DEBUG_ASSERT(t->magic == THREAD_MAGIC);
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    if (t->state != THREAD_BLOCKED)
        return ZX_ERR_BAD_STATE;

    DEBUG_ASSERT(t->blocking_wait_queue != NULL);
    DEBUG_ASSERT(t->blocking_wait_queue->magic == WAIT_QUEUE_MAGIC);
    DEBUG_ASSERT(list_in_list(&t->queue_node));

    list_delete(&t->queue_node);
    t->blocking_wait_queue->count--;
    t->blocking_wait_queue = NULL;
    t->blocked_status = wait_queue_error;

    *local_resched = sched_unblock(t);

    return ZX_OK;
}


