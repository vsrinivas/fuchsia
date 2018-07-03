// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/defines.h>
#include <arch/ops.h>
#include <arch/thread.h>
#include <debug.h>
#include <kernel/cpu.h>
#include <kernel/spinlock.h>
#include <kernel/wait.h>
#include <list.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// debug-enable runtime checks
#define THREAD_STACK_BOUNDS_CHECK (LK_DEBUGLEVEL > 2)
#ifndef THREAD_STACK_PADDING_SIZE
#define THREAD_STACK_PADDING_SIZE 256
#endif

#define STACK_DEBUG_BYTE (0x99)
#define STACK_DEBUG_WORD (0x99999999)

enum thread_state {
    THREAD_INITIAL = 0,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_SLEEPING,
    THREAD_SUSPENDED,
    THREAD_DEATH,
};

enum thread_user_state_change {
    THREAD_USER_STATE_EXIT,
    THREAD_USER_STATE_SUSPEND,
    THREAD_USER_STATE_RESUME,
};

typedef int (*thread_start_routine)(void* arg);
typedef void (*thread_trampoline_routine)(void) __NO_RETURN;
typedef void (*thread_user_callback_t)(enum thread_user_state_change new_state,
                                       void* user_thread);
typedef void (*thread_tls_callback_t)(void* tls_value);

// clang-format off
#define THREAD_FLAG_DETACHED                 (1 << 0)
#define THREAD_FLAG_FREE_STACK               (1 << 1)
#define THREAD_FLAG_FREE_STRUCT              (1 << 2)
#define THREAD_FLAG_REAL_TIME                (1 << 3)
#define THREAD_FLAG_IDLE                     (1 << 4)
#define THREAD_FLAG_DEBUG_STACK_BOUNDS_CHECK (1 << 5)

#define THREAD_SIGNAL_KILL                   (1 << 0)
#define THREAD_SIGNAL_SUSPEND                (1 << 1)
#define THREAD_SIGNAL_POLICY_EXCEPTION       (1 << 2)
// clang-format on

#define THREAD_MAGIC (0x74687264) // 'thrd'

// This includes the trailing NUL.
// N.B. This must match ZX_MAX_NAME_LEN.
#define THREAD_NAME_LENGTH 32

#define THREAD_LINEBUFFER_LENGTH 128

// Number of kernel tls slots.
#define THREAD_MAX_TLS_ENTRY 2

struct vmm_aspace;

typedef struct thread {
    int magic;
    struct list_node thread_list_node;

    // active bits
    struct list_node queue_node;
    enum thread_state state;
    zx_time_t last_started_running;
    zx_duration_t remaining_time_slice;
    unsigned int flags;
    unsigned int signals;

    // Total time in THREAD_RUNNING state.  If the thread is currently in
    // THREAD_RUNNING state, this excludes the time it has accrued since it
    // left the scheduler.
    zx_duration_t runtime_ns;

    // priority: in the range of [MIN_PRIORITY, MAX_PRIORITY], from low to high.
    // base_priority is set at creation time, and can be tuned with thread_set_priority().
    // priority_boost is a signed value that is moved around within a range by the scheduler.
    // inherited_priority is temporarily set to >0 when inheriting a priority from another
    // thread blocked on a locking primitive this thread holds. -1 means no inherit.
    // effective_priority is MAX(base_priority + priority boost, inherited_priority) and is
    // the working priority for run queue decisions.
    int effec_priority;
    int base_priority;
    int priority_boost;
    int inherited_priority;

    // current cpu the thread is either running on or in the ready queue, undefined otherwise
    cpu_num_t curr_cpu;
    cpu_num_t last_cpu;      // last cpu the thread ran on, INVALID_CPU if it's never run
    cpu_mask_t cpu_affinity; // mask of cpus that this thread can run on

    // if blocked, a pointer to the wait queue
    struct wait_queue* blocking_wait_queue;

    // list of other wait queue heads if we're a head
    struct list_node wait_queue_heads_node;

    // return code if woken up abnormally from suspend, sleep, or block
    zx_status_t blocked_status;

    // are we allowed to be interrupted on the current thing we're blocked/sleeping on
    bool interruptable;

    // number of mutexes we currently hold
    int mutexes_held;

    // pointer to the kernel address space this thread is associated with
    struct vmm_aspace* aspace;

    // pointer to user thread if one exists for this thread
    void* user_thread;
    uint64_t user_tid;
    uint64_t user_pid;

    // callback for user thread state changes; do not invoke directly, use invoke_user_callback
    // helper function instead
    thread_user_callback_t user_callback;

    // non-NULL if stopped in an exception
    const struct arch_exception_context* exception_context;

    // architecture stuff
    struct arch_thread arch;

    // stack stuff
    void* stack;
    size_t stack_size;
    vaddr_t stack_top;
#if __has_feature(safe_stack)
    void* unsafe_stack;
#endif

    // entry point
    thread_start_routine entry;
    void* arg;

    // return code
    int retcode;
    struct wait_queue retcode_wait_queue;

    // disable_counts contains two fields:
    //
    //  * Bottom 16 bits: the preempt_disable counter.  See
    //    thread_preempt_disable().
    //  * Top 16 bits: the resched_disable counter.  See
    //    thread_resched_disable().
    //
    // This is a single field so that both counters can be compared against
    // zero with a single memory access and comparison.
    //
    // disable_counts is modified by interrupt handlers, but it is always
    // restored to its original value before the interrupt handler returns,
    // so modifications are not visible to the interrupted thread.  Despite
    // that, "volatile" is still technically needed.  Otherwise the
    // compiler is technically allowed to compile
    // "++thread->disable_counts" into code that stores a junk value into
    // preempt_disable temporarily.
    volatile uint32_t disable_counts;

    // preempt_pending tracks whether a thread reschedule is pending.
    //
    // This is volatile because it can be changed asynchronously by an
    // interrupt handler: If preempt_disable is set, an interrupt handler
    // may change this from false to true.  Otherwise, if resched_disable
    // is set, an interrupt handler may change this from true to false.
    //
    // preempt_pending should only be true:
    //  * if preempt_disable or resched_disable are non-zero, or
    //  * after preempt_disable or resched_disable have been decremented,
    //    while preempt_pending is being checked.
    volatile bool preempt_pending;

    // thread local storage, intialized to zero
    void* tls[THREAD_MAX_TLS_ENTRY];

    // callback for cleanup of tls slots
    thread_tls_callback_t tls_callback[THREAD_MAX_TLS_ENTRY];

    char name[THREAD_NAME_LENGTH];
#if WITH_DEBUG_LINEBUFFER
    // buffering for debug/klog output
    int linebuffer_pos;
    char linebuffer[THREAD_LINEBUFFER_LENGTH];
#endif
} thread_t;

// thread priority
#define NUM_PRIORITIES (32)
#define LOWEST_PRIORITY (0)
#define HIGHEST_PRIORITY (NUM_PRIORITIES - 1)
#define DPC_PRIORITY (NUM_PRIORITIES - 2)
#define IDLE_PRIORITY LOWEST_PRIORITY
#define LOW_PRIORITY (NUM_PRIORITIES / 4)
#define DEFAULT_PRIORITY (NUM_PRIORITIES / 2)
#define HIGH_PRIORITY ((NUM_PRIORITIES / 4) * 3)

// stack size
#ifdef CUSTOM_DEFAULT_STACK_SIZE
#define DEFAULT_STACK_SIZE CUSTOM_DEFAULT_STACK_SIZE
#else
#define DEFAULT_STACK_SIZE ARCH_DEFAULT_STACK_SIZE
#endif

// functions
void thread_init_early(void);
void thread_become_idle(void) __NO_RETURN;
void thread_secondary_cpu_init_early(thread_t* t);
void thread_secondary_cpu_entry(void) __NO_RETURN;
void thread_construct_first(thread_t* t, const char* name);
thread_t* thread_create_idle_thread(uint cpu_num);
void thread_set_name(const char* name);
void thread_set_priority(thread_t* t, int priority);
void thread_set_user_callback(thread_t* t, thread_user_callback_t cb);
thread_t* thread_create(const char* name, thread_start_routine entry, void* arg, int priority, size_t stack_size);
thread_t* thread_create_etc(thread_t* t, const char* name, thread_start_routine entry, void* arg, int priority, void* stack, void* unsafe_stack, size_t stack_size, thread_trampoline_routine alt_trampoline);
void thread_resume(thread_t*);
zx_status_t thread_suspend(thread_t*);
void thread_signal_policy_exception(void);
void thread_exit(int retcode) __NO_RETURN;
void thread_forget(thread_t*);

// set the mask of valid cpus to run the thread on. migrates the thread to satisfy
// the new constraint
void thread_set_cpu_affinity(thread_t* t, cpu_mask_t mask);

// migrates the current thread to the CPU identified by target_cpu
void thread_migrate_to_cpu(cpu_num_t target_cpuid);

zx_status_t thread_detach(thread_t* t);
zx_status_t thread_join(thread_t* t, int* retcode, zx_time_t deadline);
zx_status_t thread_detach_and_resume(thread_t* t);
zx_status_t thread_set_real_time(thread_t* t);

// scheduler routines to be used by regular kernel code
void thread_yield(void);      // give up the cpu and time slice voluntarily
void thread_preempt(void);    // get preempted at irq time
void thread_reschedule(void); // revaluate the run queue on the current cpu,
                              // can be used after waking up threads

void thread_owner_name(thread_t* t, char out_name[THREAD_NAME_LENGTH]);

// print the backtrace on the current thread
void thread_print_current_backtrace(void);

// append the backtrace of the current thread to the passed in char pointer up
// to `len' characters.
// return the number of chars appended.
size_t thread_append_current_backtrace(char* out, size_t len);

// print the backtrace of the passed in thread, if possible
zx_status_t thread_print_backtrace(thread_t* t);

// Return true if stopped in an exception.
static inline bool thread_stopped_in_exception(const thread_t* thread) {
    return !!thread->exception_context;
}

// wait until after the specified deadline. interruptable may return early with
// ZX_ERR_INTERNAL_INTR_KILLED if thread is signaled for kill.
zx_status_t thread_sleep_etc(zx_time_t deadline, bool interruptable);

// non interruptable version of thread_sleep_etc
static inline zx_status_t thread_sleep(zx_time_t deadline) {
    return thread_sleep_etc(deadline, false);
}

// non-interruptable relative delay version of thread_sleep
zx_status_t thread_sleep_relative(zx_duration_t delay);

// return the number of nanoseconds a thread has been running for
zx_duration_t thread_runtime(const thread_t* t);

// deliver a kill signal to a thread
void thread_kill(thread_t* t);

// return true if thread has been signaled
static inline bool thread_is_signaled(thread_t* t) {
    return t->signals != 0;
}

// process pending signals, may never return because of kill signal
void thread_process_pending_signals(void);

void dump_thread(thread_t* t, bool full);
void arch_dump_thread(thread_t* t);
void dump_all_threads(bool full);
void dump_all_threads_locked(bool full);
void dump_thread_user_tid(uint64_t tid, bool full);
void dump_thread_user_tid_locked(uint64_t tid, bool full);

// find a thread based on the thread id
// NOTE: used only for debugging, its a slow linear search through the
// global thread list
thread_t* thread_id_to_thread_slow(uint64_t tid);

static inline bool thread_is_realtime(thread_t* t) {
    return (t->flags & THREAD_FLAG_REAL_TIME) && t->base_priority > DEFAULT_PRIORITY;
}

static inline bool thread_is_idle(thread_t* t) {
    return !!(t->flags & THREAD_FLAG_IDLE);
}

static inline bool thread_is_real_time_or_idle(thread_t* t) {
    return !!(t->flags & (THREAD_FLAG_REAL_TIME | THREAD_FLAG_IDLE));
}

// the current thread
#include <arch/current_thread.h>
thread_t* get_current_thread(void);
void set_current_thread(thread_t*);

// scheduler lock
extern spin_lock_t thread_lock;

#define THREAD_LOCK(state)         \
    spin_lock_saved_state_t state; \
    spin_lock_irqsave(&thread_lock, state)
#define THREAD_UNLOCK(state) spin_unlock_irqrestore(&thread_lock, state)

static inline bool thread_lock_held(void) {
    return spin_lock_held(&thread_lock);
}

// Thread local storage. See tls_slots.h in the object layer above for
// the current slot usage.

static inline void* tls_get(uint entry) {
    return get_current_thread()->tls[entry];
}

static inline void* tls_set(uint entry, void* val) {
    thread_t* curr_thread = get_current_thread();
    void* oldval = curr_thread->tls[entry];
    curr_thread->tls[entry] = val;
    return oldval;
}

// set the callback that is issued when the thread exits
static inline void tls_set_callback(uint entry, thread_tls_callback_t cb) {
    get_current_thread()->tls_callback[entry] = cb;
}

void thread_check_preempt_pending(void);

static inline uint32_t thread_preempt_disable_count(void) {
    return get_current_thread()->disable_counts & 0xffff;
}

static inline uint32_t thread_resched_disable_count(void) {
    return get_current_thread()->disable_counts >> 16;
}

// thread_preempt_disable() increments the preempt_disable counter for the
// current thread.  While preempt_disable is non-zero, preemption of the
// thread is disabled, including preemption from interrupt handlers.
// During this time, any call to thread_reschedule() or sched_reschedule()
// will only record that a reschedule is pending, and won't do a context
// switch.
//
// Note that this does not disallow blocking operations
// (e.g. mutex_acquire()).  Disabling preemption does not prevent switching
// away from the current thread if it blocks.
//
// A call to thread_preempt_disable() must be matched by a later call to
// thread_preempt_reenable() to decrement the preempt_disable counter.
static inline void thread_preempt_disable(void) {
    DEBUG_ASSERT(thread_preempt_disable_count() < 0xffff);

    thread_t* current_thread = get_current_thread();
    atomic_signal_fence();
    ++current_thread->disable_counts;
    atomic_signal_fence();
}

// thread_preempt_reenable() decrements the preempt_disable counter.  See
// thread_preempt_disable().
static inline void thread_preempt_reenable(void) {
    DEBUG_ASSERT(!arch_in_int_handler());
    DEBUG_ASSERT(thread_preempt_disable_count() > 0);

    thread_t* current_thread = get_current_thread();
    atomic_signal_fence();
    uint32_t new_count = --current_thread->disable_counts;
    atomic_signal_fence();

    if (new_count == 0)
        thread_check_preempt_pending();
}

// This is the same as thread_preempt_reenable(), except that it does not
// check for any pending reschedules.  This is useful in interrupt handlers
// when we know that no reschedules should have become pending since
// calling thread_preempt_disable().
static inline void thread_preempt_reenable_no_resched(void) {
    DEBUG_ASSERT(thread_preempt_disable_count() > 0);

    thread_t* current_thread = get_current_thread();
    atomic_signal_fence();
    --current_thread->disable_counts;
    atomic_signal_fence();
}

// thread_resched_disable() increments the resched_disable counter for the
// current thread.  When resched_disable is non-zero, preemption of the
// thread from outside interrupt handlers is disabled.  However, interrupt
// handlers may still preempt the thread.
//
// This is a weaker version of thread_preempt_disable().
//
// As with preempt_disable, blocking operations are still allowed while
// resched_disable is non-zero.
//
// A call to thread_resched_disable() must be matched by a later call to
// thread_resched_reenable() to decrement the preempt_disable counter.
static inline void thread_resched_disable(void) {
    DEBUG_ASSERT(thread_resched_disable_count() < 0xffff);

    thread_t* current_thread = get_current_thread();
    atomic_signal_fence();
    current_thread->disable_counts += 1 << 16;
    atomic_signal_fence();
}

// thread_resched_reenable() decrements the preempt_disable counter.  See
// thread_resched_disable().
static inline void thread_resched_reenable(void) {
    DEBUG_ASSERT(!arch_in_int_handler());
    DEBUG_ASSERT(thread_resched_disable_count() > 0);

    thread_t* current_thread = get_current_thread();
    atomic_signal_fence();
    uint32_t new_count = current_thread->disable_counts - (1 << 16);
    current_thread->disable_counts = new_count;
    atomic_signal_fence();

    if (new_count == 0)
        thread_check_preempt_pending();
}

// thread_preempt_set_pending() marks a preemption as pending for the
// current CPU.
//
// This is similar to thread_reschedule(), except that it may only be
// used inside an interrupt handler while interrupts and preemption
// are disabled, between thread_preempt_disable() and
// thread_preempt_reenable().  It is similar to sched_reschedule(),
// except that it does not need to be called with thread_lock held.
static inline void thread_preempt_set_pending(void) {
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(arch_in_int_handler());
    thread_t* current_thread = get_current_thread();
    DEBUG_ASSERT(thread_preempt_disable_count() > 0);

    current_thread->preempt_pending = true;
}

__END_CDECLS

#ifdef __cplusplus

#include <fbl/macros.h>

class AutoThreadLock {
public:
    AutoThreadLock() TA_ACQ(thread_lock) {
        spin_lock_irqsave(&thread_lock, state_);
    }

    ~AutoThreadLock() TA_REL(thread_lock) {
        spin_unlock_irqrestore(&thread_lock, state_);
    }

    DISALLOW_COPY_ASSIGN_AND_MOVE(AutoThreadLock);

private:
    spin_lock_saved_state_t state_;
};

// AutoReschedDisable is an RAII helper for disabling rescheduling
// using thread_resched_disable()/thread_resched_reenable().
//
// A typical use case is when we wake another thread while holding a
// mutex.  If the other thread is likely to claim the same mutex when
// runs (either immediately or later), then it is useful to defer
// waking the thread until after we have released the mutex.  We can
// do that by disabling rescheduling while holding the lock.  This is
// beneficial when there are no free CPUs for running the woken thread
// on.
//
// Example usage:
//
//   AutoReschedDisable resched_disable;
//   AutoLock al(&lock_);
//   // Do some initial computation...
//   resched_disable.Disable();
//   // Possibly wake another thread...
//
// The AutoReschedDisable must be placed before the AutoLock to ensure that
// rescheduling is re-enabled only after releasing the mutex.
class AutoReschedDisable {
public:
    AutoReschedDisable() {}
    ~AutoReschedDisable() {
        if (started_) {
            thread_resched_reenable();
        }
    }

    void Disable() {
        if (!started_) {
            thread_resched_disable();
            started_ = true;
        }
    }

    DISALLOW_COPY_ASSIGN_AND_MOVE(AutoReschedDisable);

private:
    bool started_ = false;
};

#endif // __cplusplus
