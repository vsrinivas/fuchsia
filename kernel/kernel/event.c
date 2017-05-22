// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


/**
 * @file
 * @brief  Event wait and signal functions for threads.
 * @defgroup event Events
 *
 * An event is a subclass of a wait queue.
 *
 * Threads wait for events, with optional timeouts.
 *
 * Events are "signaled", releasing waiting threads to continue.
 * Signals may be one-shot signals (EVENT_FLAG_AUTOUNSIGNAL), in which
 * case one signal releases only one thread, at which point it is
 * automatically cleared. Otherwise, signals release all waiting threads
 * to continue immediately until the signal is manually cleared with
 * event_unsignal().
 *
 * @{
 */

#include <kernel/event.h>
#include <debug.h>
#include <assert.h>
#include <err.h>
#include <kernel/thread.h>

/**
 * @brief  Initialize an event object
 *
 * @param e        Event object to initialize
 * @param initial  Initial value for "signaled" state
 * @param flags    0 or EVENT_FLAG_AUTOUNSIGNAL
 */
void event_init(event_t *e, bool initial, uint flags)
{
    *e = (event_t)EVENT_INITIAL_VALUE(*e, initial, flags);
}

/**
 * @brief  Destroy an event object.
 *
 * Event's resources are freed and it may no longer be
 * used until event_init() is called again.  Any threads
 * still waiting on the event will be resumed.
 *
 * @param e        Event object to initialize
 */
void event_destroy(event_t *e)
{
    DEBUG_ASSERT(e->magic == EVENT_MAGIC);

    THREAD_LOCK(state);

    e->magic = 0;
    e->signaled = false;
    e->flags = 0;
    wait_queue_destroy(&e->wait);

    THREAD_UNLOCK(state);
}

/**
 * @brief  Wait for event to be signaled
 *
 * If the event has already been signaled, this function
 * returns immediately.  Otherwise, the current thread
 * goes to sleep until the event object is signaled,
 * the deadline is reached, or the event object is destroyed
 * by another thread.
 *
 * @param e        Event object
 * @param deadline Deadline to abort at, in ns
 * @param interruptable  Allowed to interrupt if thread is signaled
 *
 * @return  0 on success, ERR_TIMED_OUT on timeout,
 *          other values depending on wait_result value
 *          when event_signal_etc is used.
 */
status_t event_wait_deadline(event_t *e, lk_time_t deadline, bool interruptable)
{
    thread_t *current_thread = get_current_thread();
    status_t ret = NO_ERROR;

    DEBUG_ASSERT(e->magic == EVENT_MAGIC);
    DEBUG_ASSERT(!arch_in_int_handler());

    THREAD_LOCK(state);

    current_thread->interruptable = interruptable;

    if (e->signaled) {
        /* signaled, we're going to fall through */
        if (e->flags & EVENT_FLAG_AUTOUNSIGNAL) {
            /* autounsignal flag lets one thread fall through before unsignaling */
            e->signaled = false;
        }
    } else {
        /* unsignaled, block here */
        ret = wait_queue_block(&e->wait, deadline);
    }

    current_thread->interruptable = false;

out:
    THREAD_UNLOCK(state);

    return ret;
}

static int event_signal_internal(event_t *e, bool reschedule, status_t wait_result, bool thread_lock_held)
{
    DEBUG_ASSERT(e->magic == EVENT_MAGIC);
    DEBUG_ASSERT(!reschedule || !arch_in_int_handler());

    // conditionally acquire/release the thread lock
    // NOTE: using the manual spinlock grab/release instead of THREAD_LOCK because
    // the state variable needs to exit in either path.
    spin_lock_saved_state_t state = 0;
    if (!thread_lock_held)
        spin_lock_irqsave(&thread_lock, state);

    int wake_count = 0;

    if (!e->signaled) {
        if (e->flags & EVENT_FLAG_AUTOUNSIGNAL) {
            /* try to release one thread and leave unsignaled if successful */
            if ((wake_count = wait_queue_wake_one(&e->wait, reschedule, wait_result)) <= 0) {
                /*
                 * if we didn't actually find a thread to wake up, go to
                 * signaled state and let the next call to event_wait
                 * unsignal the event.
                 */
                e->signaled = true;
            }
        } else {
            /* release all threads and remain signaled */
            e->signaled = true;
            wake_count = wait_queue_wake_all(&e->wait, reschedule, wait_result);
        }
    }

    // conditionally THREAD_UNLOCK
    if (!thread_lock_held)
        spin_unlock_irqrestore(&thread_lock, state);

    return wake_count;
}

/**
 * @brief  Signal an event
 *
 * Signals an event.  If EVENT_FLAG_AUTOUNSIGNAL is set in the event
 * object's flags, only one waiting thread is allowed to proceed.  Otherwise,
 * all waiting threads are allowed to proceed until such time as
 * event_unsignal() is called.
 *
 * @param e           Event object
 * @param reschedule  If true, waiting thread(s) are executed immediately,
 *                    and the current thread resumes only after the
 *                    waiting threads have been satisfied. If false,
 *                    waiting threads are placed at the head of the run
 *                    queue.
 * @param wait_result What status event_wait_deadline will return to the
 *                    thread or threads that are woken up.
 *
 * @return  Returns the number of threads that have been unblocked.
 */
int event_signal_etc(event_t *e, bool reschedule, status_t wait_result)
{
    return event_signal_internal(e, reschedule, wait_result, false);
}

/**
 * @brief  Signal an event
 *
 * Signals an event.  If EVENT_FLAG_AUTOUNSIGNAL is set in the event
 * object's flags, only one waiting thread is allowed to proceed.  Otherwise,
 * all waiting threads are allowed to proceed until such time as
 * event_unsignal() is called.
 *
 * @param e           Event object
 * @param reschedule  If true, waiting thread(s) are executed immediately,
 *                    and the current thread resumes only after the
 *                    waiting threads have been satisfied. If false,
 *                    waiting threads are placed at the head of the run
 *                    queue.
 *
 * @return  Returns the number of threads that have been unblocked.
 */
int event_signal(event_t *e, bool reschedule)
{
    return event_signal_internal(e, reschedule, NO_ERROR, false);
}

/* same as above, but the thread lock must already be held */
int event_signal_thread_locked(event_t *e)
{
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    return event_signal_internal(e, false, NO_ERROR, true);
}

/**
 * @brief  Clear the "signaled" property of an event
 *
 * Used mainly for event objects without the EVENT_FLAG_AUTOUNSIGNAL
 * flag.  Once this function is called, threads that call event_wait()
 * functions will once again need to wait until the event object
 * is signaled.
 *
 * @param e  Event object
 *
 * @return  Returns NO_ERROR on success.
 */
status_t event_unsignal(event_t *e)
{
    DEBUG_ASSERT(e->magic == EVENT_MAGIC);

    e->signaled = false;

    return NO_ERROR;
}

