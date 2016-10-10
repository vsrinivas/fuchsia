// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef __KERNEL_THREAD_H
#define __KERNEL_THREAD_H

#include <sys/types.h>
#include <list.h>
#include <magenta/compiler.h>
#include <arch/defines.h>
#include <arch/ops.h>
#include <arch/thread.h>
#include <kernel/wait.h>
#include <kernel/spinlock.h>
#include <debug.h>

#if WITH_KERNEL_VM
#include <kernel/vm.h>
#endif

__BEGIN_CDECLS;

/* debug-enable runtime checks */
#if LK_DEBUGLEVEL > 1
#define THREAD_STATS 1
#define THREAD_STACK_BOUNDS_CHECK 1
#ifndef THREAD_STACK_PADDING_SIZE
#define THREAD_STACK_PADDING_SIZE 256
#endif
#endif

enum thread_state {
    THREAD_SUSPENDED = 0,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_SLEEPING,
    THREAD_DEATH,
};

typedef int (*thread_start_routine)(void *arg);
typedef void (*thread_trampoline_routine)(void) __NO_RETURN;
typedef void (*thread_exit_callback_t)(void *arg);

/* thread local storage */
enum thread_tls_list {
#ifdef WITH_LIB_UTHREAD
    TLS_ENTRY_UTHREAD,
#endif
#ifdef WITH_LIB_USERBOOT
    TLS_ENTRY_LKUSER,
#endif
    MAX_TLS_ENTRY
};

#define THREAD_FLAG_DETACHED                  (1<<0)
#define THREAD_FLAG_FREE_STACK                (1<<1)
#define THREAD_FLAG_FREE_STRUCT               (1<<2)
#define THREAD_FLAG_REAL_TIME                 (1<<3)
#define THREAD_FLAG_IDLE                      (1<<4)
#define THREAD_FLAG_DEBUG_STACK_BOUNDS_CHECK  (1<<5)
#define THREAD_FLAG_STOPPED_FOR_EXCEPTION     (1<<6)

#define THREAD_SIGNAL_KILL                    (1<<0)

#define THREAD_MAGIC (0x74687264) // 'thrd'

#define THREAD_NAME_LENGTH 64

#define THREAD_LINEBUFFER_LENGTH 128

typedef struct thread {
    int magic;
    struct list_node thread_list_node;

    /* active bits */
    struct list_node queue_node;
    int priority;
    enum thread_state state;
    int remaining_quantum;
    unsigned int flags;
    unsigned int signals;
#if WITH_SMP
    int curr_cpu;
    int pinned_cpu; /* only run on pinned_cpu if >= 0 */
#endif

    /* pointer to the kernel address space this thread is associated with */
#if WITH_KERNEL_VM
    vmm_aspace_t *aspace;
#endif

    /* pointer to user thread if one exists for this thread */
    void* user_thread;
#if WITH_LIB_KTRACE
    uint64_t user_tid;
    uint64_t user_pid;
#endif

    /* accounting information */
    lk_bigtime_t last_started_running_ns;
    /* Total time in THREAD_RUNNING state.  If the thread is currently in
     * THREAD_RUNNING state, this excludes the time it has accrued since it
     * left the scheduler. */
    lk_bigtime_t runtime_ns;

    /* if blocked, a pointer to the wait queue */
    struct wait_queue *blocking_wait_queue;

    /* return code if woken up abnornmally from suspend, sleep, or block */
    status_t blocked_status;

    /* are we allowed to be interrupted on the current thing we're blocked/sleeping on */
    bool interruptable;

    /* non-NULL if stopped in an exception */
    const struct arch_exception_context *exception_context;

    /* architecture stuff */
    struct arch_thread arch;

    /* stack stuff */
    void *stack;
    size_t stack_size;
    vaddr_t stack_top;

    /* entry point */
    thread_start_routine entry;
    void *arg;

    /* return code */
    int retcode;
    struct wait_queue retcode_wait_queue;

    /* callbacks particular events */
    thread_exit_callback_t exit_callback;
    void *exit_callback_arg;

    char name[THREAD_NAME_LENGTH];
#if WITH_DEBUG_LINEBUFFER
    /* buffering for debug/klog output */
    int linebuffer_pos;
    char linebuffer[THREAD_LINEBUFFER_LENGTH];
#endif
} thread_t;

#if WITH_SMP
#define thread_curr_cpu(t) ((t)->curr_cpu)
#define thread_pinned_cpu(t) ((t)->pinned_cpu)
#define thread_set_curr_cpu(t,c) ((t)->curr_cpu = (c))
#define thread_set_pinned_cpu(t, c) ((t)->pinned_cpu = (c))
#else
#define thread_curr_cpu(t) (0)
#define thread_pinned_cpu(t) (-1)
#define thread_set_curr_cpu(t,c) do {} while(0)
#define thread_set_pinned_cpu(t, c) do {} while(0)
#endif

/* thread priority */
#define NUM_PRIORITIES 32
#define LOWEST_PRIORITY 0
#define HIGHEST_PRIORITY (NUM_PRIORITIES - 1)
#define DPC_PRIORITY (NUM_PRIORITIES - 2)
#define IDLE_PRIORITY LOWEST_PRIORITY
#define LOW_PRIORITY (NUM_PRIORITIES / 4)
#define DEFAULT_PRIORITY (NUM_PRIORITIES / 2)
#define HIGH_PRIORITY ((NUM_PRIORITIES / 4) * 3)

/* stack size */
#ifdef CUSTOM_DEFAULT_STACK_SIZE
#define DEFAULT_STACK_SIZE CUSTOM_DEFAULT_STACK_SIZE
#else
#define DEFAULT_STACK_SIZE ARCH_DEFAULT_STACK_SIZE
#endif

/* functions */
void thread_init_early(void);
void thread_init(void);
void thread_become_idle(void) __NO_RETURN;
void thread_secondary_cpu_init_early(thread_t *t);
void thread_secondary_cpu_entry(void) __NO_RETURN;
void thread_construct_first(thread_t *t, const char *name);
thread_t *thread_create_idle_thread(uint cpu_num);
void thread_set_name(const char *name);
void thread_set_priority(int priority);
void thread_set_exit_callback(thread_t *t, thread_exit_callback_t cb, void *cb_arg);
thread_t *thread_create(const char *name, thread_start_routine entry, void *arg, int priority, size_t stack_size);
thread_t *thread_create_etc(thread_t *t, const char *name, thread_start_routine entry, void *arg, int priority, void *stack, size_t stack_size, thread_trampoline_routine alt_trampoline);
status_t thread_resume(thread_t *);
void thread_exit(int retcode) __NO_RETURN;
void thread_forget(thread_t *);

status_t thread_detach(thread_t *t);
status_t thread_join(thread_t *t, int *retcode, lk_time_t timeout);
status_t thread_detach_and_resume(thread_t *t);
status_t thread_set_real_time(thread_t *t);

/* wait for at least delay amount of time. interruptable may return early with ERR_INTERRUPTED
 * if thread is signaled for kill.
 */
status_t thread_sleep_etc(lk_time_t delay, bool interruptable);

/* non interruptable version of thread_sleep_etc */
static inline status_t thread_sleep(lk_time_t delay) { return thread_sleep_etc(delay, false); }

/* deliver a kill signal to a thread */
void thread_kill(thread_t *t, bool block);

/* process pending signals, may never return because of kill signal */
void thread_process_pending_signals(void);

void dump_thread(thread_t *t);
void arch_dump_thread(thread_t *t);
void dump_all_threads(void);

/* scheduler routines */
void thread_yield(void);             /* give up the cpu and time slice voluntarily */
void thread_preempt(bool interrupt); /* get preempted (return to head of queue and reschedule) */

#ifdef WITH_LIB_UTHREAD
void uthread_context_switch(thread_t *oldthread, thread_t *newthread);
#endif

/* called on every timer tick for the scheduler to do quantum expiration */
enum handler_return thread_timer_tick(void);

/* the current thread */
thread_t *get_current_thread(void);
void set_current_thread(thread_t *);

/* scheduler lock */
extern spin_lock_t thread_lock;

#define THREAD_LOCK(state) spin_lock_saved_state_t state; spin_lock_irqsave(&thread_lock, state)
#define THREAD_UNLOCK(state) spin_unlock_irqrestore(&thread_lock, state)

static inline bool thread_lock_held(void)
{
    return spin_lock_held(&thread_lock);
}

/* thread level statistics */
#if THREAD_STATS
struct thread_stats {
    lk_bigtime_t idle_time;
    lk_bigtime_t last_idle_timestamp;
    ulong reschedules;
    ulong context_switches;
    ulong irq_preempts;
    ulong preempts;
    ulong yields;
    ulong interrupts; /* platform code increment this */
    ulong timer_ints; /* timer code increment this */
    ulong timers; /* timer code increment this */

#if WITH_SMP
    ulong reschedule_ipis;
#endif
};

extern struct thread_stats thread_stats[SMP_MAX_CPUS];

#define THREAD_STATS_INC(name) do { thread_stats[arch_curr_cpu_num()].name++; } while(0)

#else

#define THREAD_STATS_INC(name) do { } while (0)

#endif

__END_CDECLS;

#endif
