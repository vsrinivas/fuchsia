// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


/**
 * @file
 * @brief  Kernel threading
 *
 * This file is the core kernel threading interface.
 *
 * @defgroup thread Threads
 * @{
 */
#include <debug.h>
#include <assert.h>
#include <inttypes.h>
#include <list.h>
#include <malloc.h>
#include <string.h>
#include <printf.h>
#include <err.h>
#include <kernel/sched.h>
#include <kernel/thread.h>
#include <kernel/timer.h>
#include <kernel/mp.h>
#include <kernel/vm.h>
#include <platform.h>
#include <target.h>
#include <lib/dpc.h>
#include <lib/heap.h>
#include <lib/ktrace.h>

#if WITH_LIB_MAGENTA
#include <magenta/c_user_thread.h>
#endif

struct thread_stats thread_stats[SMP_MAX_CPUS];

#define STACK_DEBUG_BYTE (0x99)
#define STACK_DEBUG_WORD (0x99999999)

#define DEBUG_THREAD_CONTEXT_SWITCH 0

#define TRACE_CONTEXT_SWITCH(str, x...) \
    do { if (DEBUG_THREAD_CONTEXT_SWITCH) printf("CS " str, ## x); } while (0)

#define THREAD_INITIAL_TIME_SLICE LK_MSEC(50)
#define THREAD_TICK_RATE          LK_MSEC(10)

/* global thread list */
static struct list_node thread_list = LIST_INITIAL_VALUE(thread_list);

/* master thread spinlock */
spin_lock_t thread_lock = SPIN_LOCK_INITIAL_VALUE;

/* the idle thread(s) (statically allocated) */
thread_t idle_threads[SMP_MAX_CPUS];

/* local routines */
void thread_resched(void);
static int idle_thread_routine(void *) __NO_RETURN;
static void thread_exit_locked(thread_t *current_thread, int retcode) __NO_RETURN;
static void thread_do_suspend(void);

/* scheduler */

#if PLATFORM_HAS_DYNAMIC_TIMER
/* preemption timer */
static timer_t preempt_timer[SMP_MAX_CPUS];
#endif

static void init_thread_struct(thread_t *t, const char *name)
{
    memset(t, 0, sizeof(thread_t));
    t->magic = THREAD_MAGIC;
    thread_set_pinned_cpu(t, -1);
    strlcpy(t->name, name, sizeof(t->name));
    wait_queue_init(&t->retcode_wait_queue);
}

static void initial_thread_func(void) __NO_RETURN;
static void initial_thread_func(void)
{
    int ret;

    /* release the thread lock that was implicitly held across the reschedule */
    spin_unlock(&thread_lock);
    arch_enable_ints();

    thread_t *ct = get_current_thread();
    ret = ct->entry(ct->arg);

    thread_exit(ret);
}

/**
 * @brief  Create a new thread
 *
 * This function creates a new thread.  The thread is initially suspended, so you
 * need to call thread_resume() to execute it.
 *
 * @param  name            Name of thread
 * @param  entry           Entry point of thread
 * @param  arg             Arbitrary argument passed to entry()
 * @param  priority        Execution priority for the thread.
 * @param  stack_size      Stack size for the thread.
 * @param  alt_trampoline  If not NULL, an alternate trampoline for the thread
 *                         to start on.
 *
 * Thread priority is an integer from 0 (lowest) to 31 (highest).  Some standard
 * prioritys are defined in <kernel/thread.h>:
 *
 *  HIGHEST_PRIORITY
 *  DPC_PRIORITY
 *  HIGH_PRIORITY
 *  DEFAULT_PRIORITY
 *  LOW_PRIORITY
 *  IDLE_PRIORITY
 *  LOWEST_PRIORITY
 *
 * Stack size is typically set to DEFAULT_STACK_SIZE
 *
 * @return  Pointer to thread object, or NULL on failure.
 */
thread_t *thread_create_etc(
        thread_t *t,
        const char *name,
        thread_start_routine entry, void *arg,
        int priority,
        void *stack, void *unsafe_stack, size_t stack_size,
        thread_trampoline_routine alt_trampoline)
{
    unsigned int flags = 0;

    if (!t) {
        t = malloc(sizeof(thread_t));
        if (!t)
            return NULL;
        flags |= THREAD_FLAG_FREE_STRUCT;
    }

    init_thread_struct(t, name);

    t->entry = entry;
    t->arg = arg;
    t->priority = priority;
    t->state = THREAD_INITIAL;
    t->signals = 0;
    t->blocking_wait_queue = NULL;
    t->blocked_status = NO_ERROR;
    t->interruptable = false;
    thread_set_last_cpu(t, 0);

    t->retcode = 0;
    wait_queue_init(&t->retcode_wait_queue);

    /* create the stack */
    if (!stack) {
#if THREAD_STACK_BOUNDS_CHECK
        stack_size += THREAD_STACK_PADDING_SIZE;
        flags |= THREAD_FLAG_DEBUG_STACK_BOUNDS_CHECK;
#endif
        t->stack = malloc(stack_size);
        if (!t->stack) {
            if (flags & THREAD_FLAG_FREE_STRUCT)
                free(t);
            return NULL;
        }
        flags |= THREAD_FLAG_FREE_STACK;
#if THREAD_STACK_BOUNDS_CHECK
        memset(t->stack, STACK_DEBUG_BYTE, THREAD_STACK_PADDING_SIZE);
#endif
    } else {
        t->stack = stack;
    }

#if __has_feature(safe_stack)
    if (!unsafe_stack) {
        DEBUG_ASSERT(!stack);
        DEBUG_ASSERT(flags & THREAD_FLAG_FREE_STACK);
        t->unsafe_stack = malloc(stack_size);
        if (!t->unsafe_stack) {
            free(t->stack);
            if (flags & THREAD_FLAG_FREE_STRUCT)
                free(t);
            return NULL;
        }
# if THREAD_STACK_BOUNDS_CHECK
        memset(t->unsafe_stack, STACK_DEBUG_BYTE, THREAD_STACK_PADDING_SIZE);
# endif
    } else {
        DEBUG_ASSERT(stack);
        t->unsafe_stack = unsafe_stack;
    }
#else
    DEBUG_ASSERT(!unsafe_stack);
#endif

    t->stack_size = stack_size;

    /* save whether or not we need to free the thread struct and/or stack */
    t->flags = flags;

    if (likely(alt_trampoline == NULL)) {
        alt_trampoline = initial_thread_func;
    }

    /* set up the initial stack frame */
    arch_thread_initialize(t, (vaddr_t)alt_trampoline);

    /* add it to the global thread list */
    THREAD_LOCK(state);
    list_add_head(&thread_list, &t->thread_list_node);
    THREAD_UNLOCK(state);

    return t;
}

thread_t *thread_create(const char *name, thread_start_routine entry, void *arg, int priority, size_t stack_size)
{
    return thread_create_etc(NULL, name, entry, arg, priority,
                             NULL, NULL, stack_size, NULL);
}

static void free_thread_resources(thread_t *t)
{
    /* free its stack and the thread structure itself */
    if (t->flags & THREAD_FLAG_FREE_STACK) {
        if (t->stack)
            free(t->stack);
#if __has_feature(safe_stack)
        if (t->unsafe_stack)
            free(t->unsafe_stack);
#endif
    }

    t->magic = 0;
    if (t->flags & THREAD_FLAG_FREE_STRUCT)
        free(t);
}

/**
 * @brief Flag a thread as real time
 *
 * @param t Thread to flag
 *
 * @return NO_ERROR on success
 */
status_t thread_set_real_time(thread_t *t)
{
    if (!t)
        return ERR_INVALID_ARGS;

    DEBUG_ASSERT(t->magic == THREAD_MAGIC);

    THREAD_LOCK(state);
#if PLATFORM_HAS_DYNAMIC_TIMER
    if (t == get_current_thread()) {
        /* if we're currently running, cancel the preemption timer. */
        timer_cancel(&preempt_timer[arch_curr_cpu_num()]);
    }
#endif
    t->flags |= THREAD_FLAG_REAL_TIME;
    THREAD_UNLOCK(state);

    return NO_ERROR;
}

/**
 * @brief  Make a suspended thread executable.
 *
 * This function is called to start a thread which has just been
 * created with thread_create() or which has been suspended with
 * thread_suspend().
 *
 * @param t  Thread to resume
 *
 * @return NO_ERROR on success.
 */
status_t thread_resume(thread_t *t)
{
    DEBUG_ASSERT(t->magic == THREAD_MAGIC);

    bool ints_disabled = arch_ints_disabled();
    bool resched = false;
    if (!ints_disabled) /* HACK, don't resced into bootstrap thread before idle thread is set up */
        resched = true;

    THREAD_LOCK(state);

    if (t->state == THREAD_DEATH) {
        THREAD_UNLOCK(state);
        // The thread is dead, resuming it is a no-op.
        return NO_ERROR;
    }

    /* Clear the suspend signal in case there is a pending suspend */
    t->signals &= ~THREAD_SIGNAL_SUSPEND;

    if (t->state == THREAD_INITIAL || t->state == THREAD_SUSPENDED) {
        sched_unblock(t, resched);
    }

    THREAD_UNLOCK(state);

    return NO_ERROR;
}

status_t thread_detach_and_resume(thread_t *t)
{
    status_t err;
    err = thread_detach(t);
    if (err < 0)
        return err;
    return thread_resume(t);
}

/**
 * @brief  Suspend a ready/running thread
 *
 * @param t  Thread to suspend
 *
 * @return NO_ERROR on success.
 */
status_t thread_suspend(thread_t *t)
{
    DEBUG_ASSERT(t->magic == THREAD_MAGIC);
    DEBUG_ASSERT(!thread_is_idle(t));

    THREAD_LOCK(state);

    switch (t->state) {
        case THREAD_INITIAL:
        case THREAD_DEATH:
            THREAD_UNLOCK(state);
            return ERR_BAD_STATE;
        case THREAD_READY:
            /* thread is ready to run and not blocked or suspended.
             * will wake up and deal with the signal soon. */
            break;
        case THREAD_RUNNING:
            /* thread is running (on another cpu) */
            mp_reschedule(1u << thread_last_cpu(t), 0);
            break;
        case THREAD_SUSPENDED:
            /* thread is suspended already */
            break;
        case THREAD_BLOCKED:
            /* thread is blocked on something and marked interruptable */
            if (t->interruptable)
                thread_unblock_from_wait_queue(t, ERR_INTERRUPTED_RETRY);
            break;
        case THREAD_SLEEPING:
            /* thread is sleeping */
            if (t->interruptable) {
                t->blocked_status = ERR_INTERRUPTED_RETRY;

                sched_unblock(t, false);
            }
            break;
    }

    t->signals |= THREAD_SIGNAL_SUSPEND;

    THREAD_UNLOCK(state);

    return NO_ERROR;
}

status_t thread_join(thread_t *t, int *retcode, lk_time_t deadline)
{
    DEBUG_ASSERT(t->magic == THREAD_MAGIC);

    THREAD_LOCK(state);

    if (t->flags & THREAD_FLAG_DETACHED) {
        /* the thread is detached, go ahead and exit */
        THREAD_UNLOCK(state);
        return ERR_BAD_STATE;
    }

    /* wait for the thread to die */
    if (t->state != THREAD_DEATH) {
        status_t err = wait_queue_block(&t->retcode_wait_queue, deadline);
        if (err < 0) {
            THREAD_UNLOCK(state);
            return err;
        }
    }

    DEBUG_ASSERT(t->magic == THREAD_MAGIC);
    DEBUG_ASSERT(t->state == THREAD_DEATH);
    DEBUG_ASSERT(t->blocking_wait_queue == NULL);
    DEBUG_ASSERT(!list_in_list(&t->queue_node));

    /* save the return code */
    if (retcode)
        *retcode = t->retcode;

    /* remove it from the master thread list */
    list_delete(&t->thread_list_node);

    /* clear the structure's magic */
    t->magic = 0;

    THREAD_UNLOCK(state);

    free_thread_resources(t);

    return NO_ERROR;
}

status_t thread_detach(thread_t *t)
{
    DEBUG_ASSERT(t->magic == THREAD_MAGIC);

    THREAD_LOCK(state);

    /* if another thread is blocked inside thread_join() on this thread,
     * wake them up with a specific return code */
    wait_queue_wake_all(&t->retcode_wait_queue, false, ERR_BAD_STATE);

    /* if it's already dead, then just do what join would have and exit */
    if (t->state == THREAD_DEATH) {
        t->flags &= ~THREAD_FLAG_DETACHED; /* makes sure thread_join continues */
        THREAD_UNLOCK(state);
        return thread_join(t, NULL, 0);
    } else {
        t->flags |= THREAD_FLAG_DETACHED;
        THREAD_UNLOCK(state);
        return NO_ERROR;
    }
}

/* called back in the DPC worker thread to free the stack and/or the thread structure
 * itself for a thread that is exiting on its own.
 */
static void thread_free_dpc(struct dpc *dpc)
{
    thread_t *t = (thread_t *)dpc->arg;

    DEBUG_ASSERT(t->magic == THREAD_MAGIC);
    DEBUG_ASSERT(t->state == THREAD_DEATH);

    /* grab and release the thread lock, which effectively serializes us with
     * the thread that is queuing itself for destruction.
     */
    THREAD_LOCK(state);
    CF;
    THREAD_UNLOCK(state);

    free_thread_resources(t);
}

__NO_RETURN static void thread_exit_locked(thread_t *current_thread, int retcode)
{
    /* create a dpc on the stack to queue up a free */
    /* must be put at top scope in this function to force the compiler to keep it from
     * reusing the stack before the function exits
     */
    dpc_t free_dpc;

    /* enter the dead state */
    current_thread->state = THREAD_DEATH;
    current_thread->retcode = retcode;

    /* if we're detached, then do our teardown here */
    if (current_thread->flags & THREAD_FLAG_DETACHED) {
        /* remove it from the master thread list */
        list_delete(&current_thread->thread_list_node);

        /* if we have to do any freeing of either the stack or the thread structure, queue
         * a dpc to do the cleanup
         */
        if ((current_thread->flags & THREAD_FLAG_FREE_STACK && current_thread->stack) ||
            current_thread->flags & THREAD_FLAG_FREE_STRUCT) {
            free_dpc.func = thread_free_dpc;
            free_dpc.arg = (void *)current_thread;
            dpc_queue_thread_locked(&free_dpc);
        }
    } else {
        /* signal if anyone is waiting */
        wait_queue_wake_all(&current_thread->retcode_wait_queue, false, 0);
    }

    /* reschedule */
    thread_resched();

    panic("somehow fell through thread_exit()\n");
}

/**
 * @brief Remove this thread from the scheduler, discarding
 * its execution state.
 *
 * This is almost certainly not the function you want.  In the general case,
 * this is incredibly unsafe.
 *
 * This will free any resources allocated by thread_create.
 */
void thread_forget(thread_t *t)
{
    THREAD_LOCK(state);

    __UNUSED thread_t *current_thread = get_current_thread();
    DEBUG_ASSERT(current_thread != t);

    list_delete(&t->thread_list_node);
    THREAD_UNLOCK(state);

    DEBUG_ASSERT(!list_in_list(&t->queue_node));

    free_thread_resources(t);
}

/**
 * @brief  Terminate the current thread
 *
 * Current thread exits with the specified return code.
 *
 * This function does not return.
 */
void thread_exit(int retcode)
{
    thread_t *current_thread = get_current_thread();

    DEBUG_ASSERT(current_thread->magic == THREAD_MAGIC);
    DEBUG_ASSERT(current_thread->state == THREAD_RUNNING);
    DEBUG_ASSERT(!thread_is_idle(current_thread));

    /* if the thread has a callback set, call it here */
    if (current_thread->user_callback) {
        current_thread->user_callback(THREAD_USER_STATE_EXIT, current_thread->user_thread);
    }

    THREAD_LOCK(state);

    thread_exit_locked(current_thread, retcode);
}

/* kill a thread, optionally waiting for it to die */
void thread_kill(thread_t *t, bool block)
{
    DEBUG_ASSERT(t->magic == THREAD_MAGIC);

    THREAD_LOCK(state);

    /* deliver a signal to the thread */
    /* NOTE: it's not important to do this atomically, since we're inside
     * the thread lock, but go ahead and flush it out to memory to avoid the amount
     * of races if another thread is looking at this.
     */
    t->signals |= THREAD_SIGNAL_KILL;
    smp_mb();

    /* we are killing ourself */
    if (t == get_current_thread())
        goto done;

    /* general logic is to wake up the thread so it notices it had a signal delivered to it */

    switch (t->state) {
        case THREAD_INITIAL:
            /* thread hasn't been started yet.
             * not really safe to wake it up, since it's only in this state because it's under
             * construction by the creator thread.
             */
            break;
        case THREAD_READY:
            /* thread is ready to run and not blocked or suspended.
             * will wake up and deal with the signal soon.
             */
            /* TODO: short circuit if it was blocked from user space */
            break;
        case THREAD_RUNNING:
            /* thread is running (on another cpu) */
            mp_reschedule(1u << thread_last_cpu(t), 0);
            break;
        case THREAD_SUSPENDED:
            /* thread is suspended, resume it so it can get the kill signal */
            sched_unblock(t, false);
            break;
        case THREAD_BLOCKED:
            /* thread is blocked on something and marked interruptable */
            if (t->interruptable)
                thread_unblock_from_wait_queue(t, ERR_INTERRUPTED);
            break;
        case THREAD_SLEEPING:
            /* thread is sleeping */
            if (t->interruptable) {
                t->blocked_status = ERR_INTERRUPTED;

                sched_unblock(t, false);
            }
            break;
        case THREAD_DEATH:
            /* thread is already dead */
            goto done;
    }

    /* wait for the thread to exit */
    if (block && !(t->flags & THREAD_FLAG_DETACHED)) {
        wait_queue_block(&t->retcode_wait_queue, INFINITE_TIME);
    }

done:
    THREAD_UNLOCK(state);
}

/* finish suspending the current thread */
static void thread_do_suspend(void)
{
    thread_t *current_thread = get_current_thread();
    if (current_thread->user_callback) {
        current_thread->user_callback(THREAD_USER_STATE_SUSPEND, current_thread->user_thread);
    }

    THREAD_LOCK(state);

    // Make sure the suspend signal wasn't cleared while we were running the
    // callback.
    if (current_thread->signals & THREAD_SIGNAL_SUSPEND) {
        current_thread->state = THREAD_SUSPENDED;
        current_thread->signals &= ~THREAD_SIGNAL_SUSPEND;

        thread_resched();
    }

    THREAD_UNLOCK(state);

    if (current_thread->user_callback) {
        current_thread->user_callback(THREAD_USER_STATE_RESUME, current_thread->user_thread);
    }
}

/* check for any pending signals and handle them */
void thread_process_pending_signals(void)
{
    thread_t *current_thread = get_current_thread();
    if (likely(current_thread->signals == 0))
        return;

    /* grab the thread lock so we can safely look at the signal mask */
    THREAD_LOCK(state);

    if (current_thread->signals & THREAD_SIGNAL_KILL) {
        // Ensure we don't recurse into thread_exit.
        DEBUG_ASSERT(current_thread->state != THREAD_DEATH);
        THREAD_UNLOCK(state);
        thread_exit(0);
        /* unreachable */
    } else if (current_thread->signals & THREAD_SIGNAL_SUSPEND) {
        /* transition the thread to the suspended state */
        DEBUG_ASSERT(current_thread->state == THREAD_RUNNING);
        THREAD_UNLOCK(state);
        thread_do_suspend();
    } else {
        THREAD_UNLOCK(state);
    }
}

__NO_RETURN static int idle_thread_routine(void *arg)
{
    for (;;)
        arch_idle();
}

// On ARM64 with safe-stack, it's no longer possible to use the unsafe-sp
// after set_current_thread (we'd now see newthread's unsafe-sp instead!).
// Hence this function and everything it calls between this point and the
// the low-level context switch must be marked with __NO_SAFESTACK.
__NO_SAFESTACK static void final_context_switch(thread_t *oldthread,
                                                thread_t *newthread) {
    set_current_thread(newthread);
    arch_context_switch(oldthread, newthread);
}

/**
 * @brief  Cause another thread to be executed.
 *
 * Internal reschedule routine. The current thread needs to already be in whatever
 * state and queues it needs to be in. This routine simply picks the next thread and
 * switches to it.
 *
 * This is probably not the function you're looking for. See
 * thread_yield() instead.
 */
void thread_resched(void)
{
    thread_t *current_thread = get_current_thread();
    uint cpu = arch_curr_cpu_num();

    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(spin_lock_held(&thread_lock));
    DEBUG_ASSERT(current_thread->state != THREAD_RUNNING);
    DEBUG_ASSERT(!arch_in_int_handler());

    THREAD_STATS_INC(reschedules);

    /* pick a new thread to run */
    thread_t *newthread = sched_get_top_thread(cpu);

    DEBUG_ASSERT(newthread);

    newthread->state = THREAD_RUNNING;

    thread_t *oldthread = current_thread;

    /* if it's the same thread as we're already running, exit */
    if (newthread == oldthread)
        return;

    lk_time_t now = current_time();
    oldthread->runtime_ns += now - oldthread->last_started_running;
    newthread->last_started_running = now;

    /* set up quantum for the new thread if it was consumed */
    if (newthread->remaining_time_slice == 0) {
        newthread->remaining_time_slice = THREAD_INITIAL_TIME_SLICE;
    }

    /* mark the cpu ownership of the threads */
    thread_set_last_cpu(newthread, cpu);

    /* set the cpu state based on the new thread we've picked */
    if (thread_is_idle(newthread)) {
        mp_set_cpu_idle(cpu);
    } else {
        mp_set_cpu_busy(cpu);
    }

    if (thread_is_realtime(newthread)) {
        mp_set_cpu_realtime(cpu);
    } else {
        mp_set_cpu_non_realtime(cpu);
    }

    THREAD_STATS_INC(context_switches);

    if (thread_is_idle(oldthread)) {
        thread_stats[cpu].idle_time += now - thread_stats[cpu].last_idle_timestamp;
    }
    if (thread_is_idle(newthread)) {
        thread_stats[cpu].last_idle_timestamp = now;
    }

    ktrace(TAG_CONTEXT_SWITCH, (uint32_t)newthread->user_tid, cpu | (oldthread->state << 16),
           (uint32_t)(uintptr_t)oldthread, (uint32_t)(uintptr_t)newthread);

#if PLATFORM_HAS_DYNAMIC_TIMER
    if (thread_is_real_time_or_idle(newthread)) {
        if (!thread_is_real_time_or_idle(oldthread)) {
            /* if we're switching from a non real time to a real time, cancel
             * the preemption timer. */
            TRACE_CONTEXT_SWITCH("stop preempt, cpu %u, old %p (%s), new %p (%s)\n",
                    cpu, oldthread, oldthread->name, newthread, newthread->name);
            timer_cancel(&preempt_timer[cpu]);
        }
    } else if (thread_is_real_time_or_idle(oldthread)) {
        /* if we're switching from a real time (or idle thread) to a regular one,
         * set up a periodic timer to run our preemption tick. */
        TRACE_CONTEXT_SWITCH("start preempt, cpu %u, old %p (%s), new %p (%s)\n",
                cpu, oldthread, oldthread->name, newthread, newthread->name);
        timer_set_periodic(&preempt_timer[cpu], THREAD_TICK_RATE, (timer_callback)thread_timer_tick, NULL);
    }
#endif

    /* set some optional target debug leds */
    target_set_debug_led(0, !thread_is_idle(newthread));

    TRACE_CONTEXT_SWITCH("cpu %u, old %p (%s, pri %d, flags 0x%x), new %p (%s, pri %d, flags 0x%x)\n",
            cpu, oldthread, oldthread->name, oldthread->priority,
            oldthread->flags, newthread, newthread->name,
            newthread->priority, newthread->flags);

#if THREAD_STACK_BOUNDS_CHECK
    /* check that the old thread has not blown its stack just before pushing its context */
    if (oldthread->flags & THREAD_FLAG_DEBUG_STACK_BOUNDS_CHECK) {
        static_assert((THREAD_STACK_PADDING_SIZE % sizeof(uint32_t)) == 0, "");
        uint32_t *s = (uint32_t *)oldthread->stack;
        for (size_t i = 0; i < THREAD_STACK_PADDING_SIZE / sizeof(uint32_t); i++) {
            if (unlikely(s[i] != STACK_DEBUG_WORD)) {
                /* NOTE: will probably blow the stack harder here, but hopefully enough
                 * state exists to at least get some sort of debugging done.
                 */
                panic("stack overrun at %p: thread %p (%s), stack %p\n", &s[i],
                      oldthread, oldthread->name, oldthread->stack);
            }
        }
# if __has_feature(safe_stack)
        s = (uint32_t *)oldthread->unsafe_stack;
        for (size_t i = 0; i < THREAD_STACK_PADDING_SIZE / sizeof(uint32_t); i++) {
            if (unlikely(s[i] != STACK_DEBUG_WORD)) {
                panic("unsafe_stack overrun at %p: thread %p (%s), unsafe_stack %p\n", &s[i],
                      oldthread, oldthread->name, oldthread->unsafe_stack);
            }
        }
# endif
    }
#endif

    /* see if we need to swap mmu context */
    if (newthread->aspace != oldthread->aspace) {
        vmm_context_switch(oldthread->aspace, newthread->aspace);
    }

    /* do the low level context switch */
    final_context_switch(oldthread, newthread);
}

/**
 * @brief Yield the cpu to another thread
 *
 * This function places the current thread at the end of the run queue
 * and yields the cpu to another waiting thread (if any.)
 *
 * This function will return at some later time. Possibly immediately if
 * no other threads are waiting to execute.
 */
void thread_yield(void)
{
    __UNUSED thread_t *current_thread = get_current_thread();

    DEBUG_ASSERT(current_thread->magic == THREAD_MAGIC);
    DEBUG_ASSERT(current_thread->state == THREAD_RUNNING);
    DEBUG_ASSERT(!arch_in_int_handler());

    THREAD_LOCK(state);

    THREAD_STATS_INC(yields);

    sched_yield();

    THREAD_UNLOCK(state);
}

/**
 * @brief Preempt the current thread, usually from an interrupt
 *
 * This function places the current thread at the head of the run
 * queue and then yields the cpu to another thread.
 *
 * Exception:  If the time slice for this thread has expired, then
 * the thread goes to the end of the run queue.
 *
 * This function will return at some later time. Possibly immediately if
 * no other threads are waiting to execute.
 *
 * @param interrupt for tracing purposes set if the preemption is happening
 * at interrupt context.
 *
 */
void thread_preempt(bool interrupt)
{
    thread_t *current_thread = get_current_thread();

    DEBUG_ASSERT(current_thread->magic == THREAD_MAGIC);
    DEBUG_ASSERT(current_thread->state == THREAD_RUNNING);
    DEBUG_ASSERT(!arch_in_int_handler());

    if (!thread_is_idle(current_thread)) {
        /* only track when a meaningful preempt happens */
        if (interrupt) {
            THREAD_STATS_INC(irq_preempts);
        } else {
            THREAD_STATS_INC(preempts);
        }
    }

    THREAD_LOCK(state);

    sched_preempt();

    THREAD_UNLOCK(state);
}

enum handler_return thread_timer_tick(void)
{
    thread_t *current_thread = get_current_thread();

    if (thread_is_real_time_or_idle(current_thread))
        return INT_NO_RESCHEDULE;

    current_thread->remaining_time_slice -= MIN(THREAD_TICK_RATE, current_thread->remaining_time_slice);

    ktrace_probe2("timer_tick", (uint32_t)current_thread->user_tid, current_thread->remaining_time_slice);

    if (current_thread->remaining_time_slice == 0) {
        return INT_RESCHEDULE;
    } else {
        return INT_NO_RESCHEDULE;
    }
}

/* timer callback to wake up a sleeping thread */
static enum handler_return thread_sleep_handler(timer_t *timer, lk_time_t now, void *arg)
{
    thread_t *t = (thread_t *)arg;

    DEBUG_ASSERT(t->magic == THREAD_MAGIC);

    /* spin trylocking on the thread lock since the routine that set up the callback,
     * thread_sleep_etc, may be trying to simultaneously cancel this timer while holding the
     * thread_lock.
     */
    if (timer_trylock_or_cancel(timer, &thread_lock))
        return INT_NO_RESCHEDULE;

    if (t->state != THREAD_SLEEPING) {
        spin_unlock(&thread_lock);
        return INT_NO_RESCHEDULE;
    }

    t->blocked_status = NO_ERROR;

    sched_unblock(t, false);

    spin_unlock(&thread_lock);

    return INT_RESCHEDULE;
}

/**
 * @brief  Put thread to sleep; deadline specified in ns
 *
 * This function puts the current thread to sleep until the specified
 * deadline has expired.
 *
 * Note that this function could continue to sleep after the specified deadline
 * if other threads are running.  When the deadline expires, this thread will
 * be placed at the head of the run queue.
 *
 * interruptable argument allows this routine to return early if the thread was signaled
 * for something.
 */
status_t thread_sleep_etc(lk_time_t deadline, bool interruptable)
{
    thread_t *current_thread = get_current_thread();
    status_t blocked_status;

    DEBUG_ASSERT(current_thread->magic == THREAD_MAGIC);
    DEBUG_ASSERT(current_thread->state == THREAD_RUNNING);
    DEBUG_ASSERT(!thread_is_idle(current_thread));
    DEBUG_ASSERT(!arch_in_int_handler());

    timer_t timer;
    timer_initialize(&timer);

    THREAD_LOCK(state);

    /* if we've been killed and going in interruptable, abort here */
    if (interruptable && unlikely((current_thread->signals))) {
        if (current_thread->signals & THREAD_SIGNAL_KILL) {
            blocked_status = ERR_INTERRUPTED;
        } else {
            blocked_status = ERR_INTERRUPTED_RETRY;
        }
        goto out;
    }

    if (deadline != INFINITE_TIME) {
        /* set a one shot timer to wake us up and reschedule */
        timer_set_oneshot(&timer, deadline, thread_sleep_handler, (void *)current_thread);
    }
    current_thread->state = THREAD_SLEEPING;
    current_thread->blocked_status = NO_ERROR;

    current_thread->interruptable = interruptable;
    sched_block();
    current_thread->interruptable = false;

    blocked_status = current_thread->blocked_status;

    if (deadline != INFINITE_TIME) {
        /* always cancel the timer, since we may be racing with the timer tick on other cpus */
        timer_cancel(&timer);
    }

out:
    THREAD_UNLOCK(state);

    return blocked_status;
}

status_t thread_sleep_relative(lk_time_t delay) {
    if (delay != INFINITE_TIME) {
        delay += current_time();
    }
    return thread_sleep(delay);
}

/**
 * @brief Return the number of nanoseconds a thread has been running for.
 *
 * This takes the thread_lock to ensure there are no races while calculating the
 * runtime of the thread.
 */
lk_time_t thread_runtime(const thread_t *t)
{
    THREAD_LOCK(state);

    lk_time_t runtime = t->runtime_ns;
    if (t->state == THREAD_RUNNING) {
        runtime += current_time() - t->last_started_running;
    }

    THREAD_UNLOCK(state);

    return runtime;
}

/**
 * @brief Construct a thread t around the current running state
 *
 * This should be called once per CPU initialization.  It will create
 * a thread that is pinned to the current CPU and running at the
 * highest priority.
 */
void thread_construct_first(thread_t *t, const char *name)
{
    DEBUG_ASSERT(arch_ints_disabled());
    /* Due to somethings below being macros, this might be unused on
     * non-SMP builds */
    __UNUSED uint cpu = arch_curr_cpu_num();

    init_thread_struct(t, name);
    t->priority = HIGHEST_PRIORITY;
    t->state = THREAD_RUNNING;
    t->flags = THREAD_FLAG_DETACHED;
    t->signals = 0;
    thread_set_last_cpu(t, cpu);
    thread_set_pinned_cpu(t, cpu);

    arch_thread_construct_first(t);

    THREAD_LOCK(state);
    list_add_head(&thread_list, &t->thread_list_node);
    set_current_thread(t);
    THREAD_UNLOCK(state);
}

/**
 * @brief  Initialize threading system
 *
 * This function is called once, from kmain()
 */
void thread_init_early(void)
{
    DEBUG_ASSERT(arch_curr_cpu_num() == 0);

    /* create a thread to cover the current running state */
    thread_t *t = &idle_threads[0];
    thread_construct_first(t, "bootstrap");

    sched_init_early();
}

/**
 * @brief Complete thread initialization
 *
 * This function is called once at boot time
 */
void thread_init(void)
{
#if PLATFORM_HAS_DYNAMIC_TIMER
    for (uint i = 0; i < SMP_MAX_CPUS; i++) {
        timer_initialize(&preempt_timer[i]);
    }
#endif
}

/**
 * @brief Change name of current thread
 */
void thread_set_name(const char *name)
{
    thread_t *current_thread = get_current_thread();
    strlcpy(current_thread->name, name, sizeof(current_thread->name));
}

/**
 * @brief Set the callback pointer to a function called on user thread state
 *        changes (e.g. exit, suspend, resume)
 */
void thread_set_user_callback(thread_t *t, thread_user_callback_t cb)
{
    DEBUG_ASSERT(t->state == THREAD_INITIAL);
    t->user_callback = cb;
}

/**
 * @brief Change priority of current thread
 *
 * See thread_create() for a discussion of priority values.
 */
void thread_set_priority(int priority)
{
    thread_t *current_thread = get_current_thread();

    THREAD_LOCK(state);

    if (priority <= IDLE_PRIORITY)
        priority = IDLE_PRIORITY + 1;
    if (priority > HIGHEST_PRIORITY)
        priority = HIGHEST_PRIORITY;
    current_thread->priority = priority;

    sched_preempt();

    THREAD_UNLOCK(state);
}

/**
 * @brief  Become an idle thread
 *
 * This function marks the current thread as the idle thread -- the one which
 * executes when there is nothing else to do.  This function does not return.
 * This function is called once at boot time.
 */
void thread_become_idle(void)
{
    DEBUG_ASSERT(arch_ints_disabled());

    thread_t *t = get_current_thread();

    char name[16];
    snprintf(name, sizeof(name), "idle %u", arch_curr_cpu_num());
    thread_set_name(name);

    /* mark ourself as idle */
    t->priority = IDLE_PRIORITY;
    t->flags |= THREAD_FLAG_IDLE;
    thread_set_pinned_cpu(t, arch_curr_cpu_num());

    mp_set_curr_cpu_active(true);
    mp_set_cpu_idle(arch_curr_cpu_num());

    /* enable interrupts and start the scheduler */
    arch_enable_ints();
    thread_yield();

    idle_thread_routine(NULL);
}

/**
 * @brief Create a thread around the current execution context
 */
void thread_secondary_cpu_init_early(thread_t *t)
{
    DEBUG_ASSERT(arch_ints_disabled());
    char name[16];
    snprintf(name, sizeof(name), "cpu_init %u", arch_curr_cpu_num());
    thread_construct_first(t, name);
}

void thread_secondary_cpu_entry(void)
{
    uint cpu = arch_curr_cpu_num();

    mp_set_curr_cpu_active(true);
    mp_set_cpu_idle(cpu);

    /* Exit from our bootstrap thread, and enter the scheduler on this cpu */
    thread_exit(0);
}

/**
 * @brief Create an idle thread for a secondary CPU
 */
thread_t *thread_create_idle_thread(uint cpu_num)
{
    DEBUG_ASSERT(cpu_num != 0 && cpu_num < SMP_MAX_CPUS);

    /* Shouldn't be initialized yet */
    DEBUG_ASSERT(idle_threads[cpu_num].magic != THREAD_MAGIC);

    char name[16];
    snprintf(name, sizeof(name), "idle %u", cpu_num);

    thread_t *t = thread_create_etc(
            &idle_threads[cpu_num], name,
            idle_thread_routine, NULL,
            IDLE_PRIORITY,
            NULL, NULL, DEFAULT_STACK_SIZE,
            NULL);
    if (t == NULL) {
        return t;
    }
    t->flags |= THREAD_FLAG_IDLE;
    thread_set_pinned_cpu(t, cpu_num);
    return t;
}

/**
 * @brief Return the name of the "owner" of the thread.
 *
 * Returns "kernel" if there is no owner.
 */

void thread_owner_name(thread_t *t, char out_name[THREAD_NAME_LENGTH])
{
#if WITH_LIB_MAGENTA
    if (t->user_thread) {
        magenta_thread_process_name(t->user_thread, out_name);
        return;
    }
#endif
    memcpy(out_name, "kernel", 7);
}

static const char *thread_state_to_str(enum thread_state state)
{
    switch (state) {
        case THREAD_INITIAL:
            return "init";
        case THREAD_SUSPENDED:
            return "susp";
        case THREAD_READY:
            return "rdy";
        case THREAD_RUNNING:
            return "run";
        case THREAD_BLOCKED:
            return "blok";
        case THREAD_SLEEPING:
            return "slep";
        case THREAD_DEATH:
            return "deth";
        default:
            return "unkn";
    }
}

/**
 * @brief  Dump debugging info about the specified thread.
 */
void dump_thread(thread_t *t, bool full_dump)
{
    if (t->magic != THREAD_MAGIC) {
        dprintf(INFO, "dump_thread WARNING: thread at %p has bad magic\n", t);
    }

    lk_time_t runtime = t->runtime_ns;
    if (t->state == THREAD_RUNNING) {
        runtime += current_time() - t->last_started_running;
    }

    char oname[THREAD_NAME_LENGTH];
    thread_owner_name(t, oname);

    if (full_dump) {
        dprintf(INFO, "dump_thread: t %p (%s:%s)\n", t, oname, t->name);
#if WITH_SMP
        dprintf(INFO, "\tstate %s, last_cpu %u, pinned_cpu %d, priority %d, remaining time slice %" PRIu64 "\n",
                thread_state_to_str(t->state), t->last_cpu, t->pinned_cpu, t->priority, t->remaining_time_slice);
#else
        dprintf(INFO, "\tstate %s, priority %d, remaining time slice %" PRIu64 "\n",
                thread_state_to_str(t->state), t->priority, t->remaining_time_slice);
#endif
        dprintf(INFO, "\truntime_ns %" PRIu64 ", runtime_s %" PRIu64 "\n",
                runtime, runtime / 1000000000);
        dprintf(INFO, "\tstack %p, stack_size %zu\n", t->stack, t->stack_size);
        dprintf(INFO, "\tentry %p, arg %p, flags 0x%x %s%s%s%s%s%s\n", t->entry, t->arg, t->flags,
                (t->flags & THREAD_FLAG_DETACHED) ? "Dt" :"",
                (t->flags & THREAD_FLAG_FREE_STACK) ? "Fs" :"",
                (t->flags & THREAD_FLAG_FREE_STRUCT) ? "Ft" :"",
                (t->flags & THREAD_FLAG_REAL_TIME) ? "Rt" :"",
                (t->flags & THREAD_FLAG_IDLE) ? "Id" :"",
                (t->flags & THREAD_FLAG_DEBUG_STACK_BOUNDS_CHECK) ? "Sc" :"");
        dprintf(INFO, "\twait queue %p, blocked_status %d, interruptable %d\n",
                t->blocking_wait_queue, t->blocked_status, t->interruptable);
        dprintf(INFO, "\taspace %p\n", t->aspace);
        dprintf(INFO, "\tuser_thread %p, pid %" PRIu64 ", tid %" PRIu64 "\n",
                t->user_thread, t->user_pid, t->user_tid);
        arch_dump_thread(t);
    } else {
        printf("thr %p st %4s pri %2d pid %" PRIu64 " tid %" PRIu64 " (%s:%s)\n",
               t, thread_state_to_str(t->state), t->priority, t->user_pid, t->user_tid, oname, t->name);
    }
}

/**
 * @brief  Dump debugging info about all threads
 */
void dump_all_threads(bool full)
{
    thread_t *t;

    THREAD_LOCK(state);
    list_for_every_entry(&thread_list, t, thread_t, thread_list_node) {
        if (t->magic != THREAD_MAGIC) {
            dprintf(INFO, "bad magic on thread struct %p, aborting.\n", t);
            hexdump(t, sizeof(thread_t));
            break;
        }
        dump_thread(t, full);
    }
    THREAD_UNLOCK(state);
}

/** @} */

#if WITH_LIB_KTRACE
// Used by ktrace at the start of a trace to ensure that all
// the running threads, processes, and their names are known
void ktrace_report_live_threads(void) {
    thread_t* t;

    THREAD_LOCK(state);
    list_for_every_entry(&thread_list, t, thread_t, thread_list_node) {
        DEBUG_ASSERT(t->magic == THREAD_MAGIC);
        if (t->user_tid) {
            ktrace_name(TAG_THREAD_NAME, t->user_tid, t->user_pid, t->name);
        } else {
            ktrace_name(TAG_KTHREAD_NAME, (uint32_t)(uintptr_t)t, 0, t->name);
        }
    }
    THREAD_UNLOCK(state);
}
#endif

/**
 * @defgroup  wait  Wait Queue
 * @{
 */
void wait_queue_init(wait_queue_t *wait)
{
    *wait = (wait_queue_t)WAIT_QUEUE_INITIAL_VALUE(*wait);
}

static enum handler_return wait_queue_timeout_handler(timer_t *timer, lk_time_t now, void *arg)
{
    thread_t *thread = (thread_t *)arg;

    DEBUG_ASSERT(thread->magic == THREAD_MAGIC);

    /* spin trylocking on the thread lock since the routine that set up the callback,
     * wait_queue_block, may be trying to simultaneously cancel this timer while holding the
     * thread_lock.
     */
    if (timer_trylock_or_cancel(timer, &thread_lock))
        return INT_NO_RESCHEDULE;

    enum handler_return ret = INT_NO_RESCHEDULE;
    if (thread_unblock_from_wait_queue(thread, ERR_TIMED_OUT) >= NO_ERROR) {
        ret = INT_RESCHEDULE;
    }

    spin_unlock(&thread_lock);

    return ret;
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
 * ERR_TIMED_OUT.  If the deadline is INFINITE_TIME, this function
 * waits indefinitely.  Otherwise, this function returns with
 * ERR_TIMED_OUT when the deadline occurs.
 *
 * @return ERR_TIMED_OUT on timeout, else returns the return
 * value specified when the queue was woken by wait_queue_wake_one().
 */
status_t wait_queue_block(wait_queue_t *wait, lk_time_t deadline)
{
    timer_t timer;

    thread_t *current_thread = get_current_thread();

    DEBUG_ASSERT(wait->magic == WAIT_QUEUE_MAGIC);
    DEBUG_ASSERT(current_thread->state == THREAD_RUNNING);
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    if (deadline <= current_time())
        return ERR_TIMED_OUT;

    if (current_thread->interruptable && unlikely(current_thread->signals)) {
        if (current_thread->signals & THREAD_SIGNAL_KILL) {
            return ERR_INTERRUPTED;
        } else if (current_thread->signals & THREAD_SIGNAL_SUSPEND) {
            return ERR_INTERRUPTED_RETRY;
        }
    }

    list_add_tail(&wait->list, &current_thread->queue_node);
    wait->count++;
    current_thread->state = THREAD_BLOCKED;
    current_thread->blocking_wait_queue = wait;
    current_thread->blocked_status = NO_ERROR;

    /* if the deadline is nonzero or noninfinite, set a callback to yank us out of the queue */
    if (deadline != INFINITE_TIME) {
        timer_initialize(&timer);
        timer_set_oneshot(&timer, deadline, wait_queue_timeout_handler, (void *)current_thread);
    }

    sched_block();

    /* we don't really know if the timer fired or not, so it's better safe to try to cancel it */
    if (deadline != INFINITE_TIME) {
        timer_cancel(&timer);
    }

    return current_thread->blocked_status;
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
int wait_queue_wake_one(wait_queue_t *wait, bool reschedule, status_t wait_queue_error)
{
    thread_t *t;
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

        sched_unblock(t, reschedule);

        ret = 1;
    }

    return ret;
}

thread_t *wait_queue_dequeue_one(wait_queue_t *wait, status_t wait_queue_error)
{
    thread_t *t;

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
int wait_queue_wake_all(wait_queue_t *wait, bool reschedule, status_t wait_queue_error)
{
    thread_t *t;
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

    sched_unblock_list(&list, reschedule);

    return ret;
}

bool wait_queue_is_empty(wait_queue_t *wait)
{
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
void wait_queue_destroy(wait_queue_t *wait)
{
    DEBUG_ASSERT(wait->magic == WAIT_QUEUE_MAGIC);
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

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
 * @param wait_queue_error  The return value which the new thread will receive
 *   from wait_queue_block().
 *
 * @return ERR_BAD_STATE if thread was not in any wait queue.
 */
status_t thread_unblock_from_wait_queue(thread_t *t, status_t wait_queue_error)
{
    DEBUG_ASSERT(t->magic == THREAD_MAGIC);
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    if (t->state != THREAD_BLOCKED)
        return ERR_BAD_STATE;

    DEBUG_ASSERT(t->blocking_wait_queue != NULL);
    DEBUG_ASSERT(t->blocking_wait_queue->magic == WAIT_QUEUE_MAGIC);
    DEBUG_ASSERT(list_in_list(&t->queue_node));

    list_delete(&t->queue_node);
    t->blocking_wait_queue->count--;
    t->blocking_wait_queue = NULL;
    t->blocked_status = wait_queue_error;

    sched_unblock(t, false);

    return NO_ERROR;
}

#if WITH_PANIC_BACKTRACE
static status_t thread_read_stack(thread_t* t, void* ptr, void* out, size_t sz)
{
    if (!is_kernel_address((uintptr_t)ptr) ||
        (ptr < t->stack) ||
        (ptr > (t->stack + t->stack_size - sizeof(void*)))) {
        return ERR_NOT_FOUND;
    }
    memcpy(out, ptr, sz);
    return NO_ERROR;
}

int thread_get_backtrace(thread_t* t, void* fp, thread_backtrace_t* tb)
{
    void* pc;
    if (t == NULL) {
        return -1;
    }
    int n = 0;
    for (; n < THREAD_BACKTRACE_DEPTH; n++) {
        if (thread_read_stack(t, fp + 8, &pc, sizeof(void*))) {
            break;
        }
        tb->pc[n] = pc;
        if (thread_read_stack(t, fp, &fp, sizeof(void*))) {
            break;
        }
    }
    return n;
}

void thread_print_backtrace(thread_t* t, void* fp)
{
    thread_backtrace_t tb;
    int count = thread_get_backtrace(t, fp, &tb);
    if (count < 0) {
        return;
    }

    for (int n = 0; n < count; n++) {
        printf("bt#%02d: %p\n", n, tb.pc[n]);
    }
    printf("bt#%02d: end\n", count);
}
#endif
