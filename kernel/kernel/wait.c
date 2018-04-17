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
#include <trace.h>

#define LOCAL_TRACE 0

// add expensive code to do a full validation of the wait queue at various entry points
// to this module.
#define WAIT_QUEUE_VALIDATION (0 || (LK_DEBUGLEVEL > 2))

// Wait queues are building blocks that other locking primitives use to
// handle blocking threads.
//
// Implemented as a simple structure that contains a count of the number of threads
// blocked and a list of thread_ts acting as individual queue heads, one per priority.

// +----------------+
// |                |
// |  wait_queue_t  |
// |                |
// +-------+--------+
//         |
//         |
//   +-----v-------+    +-------------+   +-------------+
//   |             +---->             +--->             |
//   |   thread_t  |    |   thread_t  |   |   thread_t  |
//   |   pri 31    |    |   pri 17    |   |   pri 8     |
//   |             <----+             <---+             |
//   +---+----^----+    +-------------+   +----+---^----+
//       |    |                                |   |
//   +---v----+----+                      +----v---+----+
//   |             |                      |             |
//   |   thread_t  |                      |   thread_t  |
//   |   pri 31    |                      |   pri 8     |
//   |             |                      |             |
//   +---+----^----+                      +-------------+
//       |    |
//   +---v----+----+
//   |             |
//   |   thread_t  |
//   |   pri 31    |
//   |             |
//   +-------------+

void wait_queue_init(wait_queue_t* wait) {
    *wait = (wait_queue_t)WAIT_QUEUE_INITIAL_VALUE(*wait);
}

void wait_queue_validate_queue(wait_queue_t* wait) {
    DEBUG_ASSERT(wait->magic == WAIT_QUEUE_MAGIC);
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    // validate that the queue is sorted properly
    thread_t* last = NULL;
    thread_t* temp;
    list_for_every_entry(&wait->heads, temp, thread_t, wait_queue_heads_node) {
        DEBUG_ASSERT(temp->magic == THREAD_MAGIC);

        // validate that the queue is sorted high to low priority
        if (last) {
            DEBUG_ASSERT_MSG(last->effec_priority > temp->effec_priority,
                    "%p:%d  %p:%d",
                    last, last->effec_priority,
                    temp, temp->effec_priority);
        }

        // walk any threads linked to this head, validating that they're the same priority
        thread_t* temp2;
        list_for_every_entry(&temp->queue_node, temp2, thread_t, queue_node) {
            DEBUG_ASSERT(temp2->magic == THREAD_MAGIC);
            DEBUG_ASSERT_MSG(temp->effec_priority == temp2->effec_priority,
                    "%p:%d  %p:%d",
                    temp, temp->effec_priority,
                    temp2, temp2->effec_priority);
        }

        last = temp;
    }
}

// add a thread to the tail of a wait queue, sorted by priority
static void wait_queue_insert(wait_queue_t* wait, thread_t* t) {
    if (likely(list_is_empty(&wait->heads))) {
        // we're the first thread
        list_initialize(&t->queue_node);
        list_add_head(&wait->heads, &t->wait_queue_heads_node);
    } else {
        int pri = t->effec_priority;

        // walk through the sorted list of wait queue heads
        thread_t* temp;
        list_for_every_entry(&wait->heads, temp, thread_t, wait_queue_heads_node) {
            if (pri > temp->effec_priority) {
                // insert ourself here as a new queue head
                list_initialize(&t->queue_node);
                list_add_before(&temp->wait_queue_heads_node, &t->wait_queue_heads_node);
                return;
            } else if (temp->effec_priority == pri) {
                // same priority, add ourself to the tail of this queue
                list_add_tail(&temp->queue_node, &t->queue_node);
                list_clear_node(&t->wait_queue_heads_node);
                return;
            }
        }

        // we walked off the end, add ourself as a new queue head at the end
        list_initialize(&t->queue_node);
        list_add_tail(&wait->heads, &t->wait_queue_heads_node);
    }
}

// remove a thread from whatever wait queue its in
// thread must be the head of a queue
static void remove_queue_head(thread_t* t) {
    // are there any nodes in the queue for this priority?
    if (list_is_empty(&t->queue_node)) {
        // no, remove ourself from the the queue list
        list_delete(&t->wait_queue_heads_node);
        list_clear_node(&t->queue_node);
    } else {
        // there are other threads in this list, make the next thread in the queue the head
        thread_t* newhead = list_peek_head_type(&t->queue_node, thread_t, queue_node);
        list_delete(&t->queue_node);

        // patch in the new head into the queue head list
        list_replace_node(&t->wait_queue_heads_node, &newhead->wait_queue_heads_node);
    }
}

// remove the head of the highest priority queue
static thread_t* wait_queue_pop_head(wait_queue_t* wait) {
    thread_t* t = NULL;

    t = list_peek_head_type(&wait->heads, thread_t, wait_queue_heads_node);
    if (!t)
        return NULL;

    remove_queue_head(t);

    return t;
}

// remove the thread from whatever wait queue its in
static void wait_queue_remove_thread(thread_t* t) {
    if (!list_in_list(&t->wait_queue_heads_node)) {
        // we're just in a queue, not a head
        list_delete(&t->queue_node);
    } else {
        // we're the head of a queue
        remove_queue_head(t);
    }
}

// return the numeric priority of the highest priority thread queued
int wait_queue_blocked_priority(wait_queue_t* wait) {
    thread_t* t = list_peek_head_type(&wait->heads, thread_t, wait_queue_heads_node);
    if (!t)
        return -1;

    return t->effec_priority;
}

// Disable thread safety analysis here since Clang has trouble with the analysis
// around timer_trylock_or_cancel.
static void wait_queue_timeout_handler(timer_t* timer, zx_time_t now,
                                       void* arg) TA_NO_THREAD_SAFETY_ANALYSIS {
    thread_t* thread = (thread_t*)arg;

    DEBUG_ASSERT(thread->magic == THREAD_MAGIC);

    /* spin trylocking on the thread lock since the routine that set up the callback,
     * wait_queue_block, may be trying to simultaneously cancel this timer while holding the
     * thread_lock.
     */
    if (timer_trylock_or_cancel(timer, &thread_lock))
        return;

    wait_queue_unblock_thread(thread, ZX_ERR_TIMED_OUT);

    spin_unlock(&thread_lock);
}

static zx_status_t wait_queue_block_worker(wait_queue_t* wait, zx_time_t deadline,
                                           uint signal_mask) {
    timer_t timer;

    thread_t* current_thread = get_current_thread();

    DEBUG_ASSERT(wait->magic == WAIT_QUEUE_MAGIC);
    DEBUG_ASSERT(current_thread->state == THREAD_RUNNING);
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    if (WAIT_QUEUE_VALIDATION) {
        wait_queue_validate_queue(wait);
    }

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

    wait_queue_insert(wait, current_thread);
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

    if (WAIT_QUEUE_VALIDATION) {
        wait_queue_validate_queue(wait);
    }

    t = wait_queue_pop_head(wait);
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

    if (WAIT_QUEUE_VALIDATION) {
        wait_queue_validate_queue(wait);
    }

    DEBUG_ASSERT(wait->magic == WAIT_QUEUE_MAGIC);
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    t = wait_queue_pop_head(wait);
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

    if (WAIT_QUEUE_VALIDATION) {
        wait_queue_validate_queue(wait);
    }

    if (wait->count == 0)
        return 0;

    struct list_node list = LIST_INITIAL_VALUE(list);

    /* pop all the threads off the wait queue into the run queue */
    /* TODO: optimize with custom pop all routine */
    while ((t = wait_queue_pop_head(wait))) {
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

    return wait->count == 0;
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

    if (wait->count != 0) {
        panic("wait_queue_destroy() called on non-empty wait_queue_t\n");
    }

    wait->magic = 0;
}

/**
 * @brief  Wake a specific thread in a wait queue
 *
 * This function extracts a specific thread from a wait queue, wakes it,
 * puts it at the head of the run queue, and does a reschedule if
 * necessary.
 *
 * @param t  The thread to wake
 * @param wait_queue_error  The return value which the new thread will receive from wait_queue_block().
 *
 * @return ZX_ERR_BAD_STATE if thread was not in any wait queue.
 */
zx_status_t wait_queue_unblock_thread(thread_t* t, zx_status_t wait_queue_error) {
    DEBUG_ASSERT(t->magic == THREAD_MAGIC);
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    if (t->state != THREAD_BLOCKED)
        return ZX_ERR_BAD_STATE;

    DEBUG_ASSERT(t->blocking_wait_queue != NULL);
    DEBUG_ASSERT(t->blocking_wait_queue->magic == WAIT_QUEUE_MAGIC);
    DEBUG_ASSERT(list_in_list(&t->queue_node));

    if (WAIT_QUEUE_VALIDATION) {
        wait_queue_validate_queue(t->blocking_wait_queue);
    }

    wait_queue_remove_thread(t);
    t->blocking_wait_queue->count--;
    t->blocking_wait_queue = NULL;
    t->blocked_status = wait_queue_error;

    if (sched_unblock(t))
        sched_reschedule();

    return ZX_OK;
}

void wait_queue_priority_changed(struct thread* t, int old_prio) {
    DEBUG_ASSERT(t->magic == THREAD_MAGIC);
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    DEBUG_ASSERT(t->state == THREAD_BLOCKED);
    DEBUG_ASSERT(t->blocking_wait_queue != NULL);
    DEBUG_ASSERT(t->blocking_wait_queue->magic == WAIT_QUEUE_MAGIC);

    LTRACEF("%p %d -> %d\n", t, old_prio, t->effec_priority);

    // simple algorithm: remove the thread from the queue and add it back
    // TODO: implement optimal algorithm depending on all the different edge
    // cases of how the thread was previously queued and what priority its
    // switching to.
    wait_queue_remove_thread(t);
    wait_queue_insert(t->blocking_wait_queue, t);

    // TODO: find a way to call into wrapper mutex object if present and
    // have the holder inherit the new priority

    if (WAIT_QUEUE_VALIDATION) {
        wait_queue_validate_queue(t->blocking_wait_queue);
    }
}

