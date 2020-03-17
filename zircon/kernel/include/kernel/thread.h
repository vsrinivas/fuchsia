// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_THREAD_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_THREAD_H_

#include <debug.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/listnode.h>
#include <zircon/syscalls/scheduler.h>
#include <zircon/types.h>

#include <arch/defines.h>
#include <arch/exception.h>
#include <arch/ops.h>
#include <arch/thread.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>
#include <kernel/cpu.h>
#include <kernel/scheduler_state.h>
#include <kernel/spinlock.h>
#include <kernel/thread_lock.h>
#include <kernel/timer.h>
#include <kernel/wait.h>
#include <lockdep/thread_lock_state.h>
#include <vm/kstack.h>

struct Thread;

// fwd decls
class OwnedWaitQueue;
class ThreadDispatcher;

enum thread_state {
  THREAD_INITIAL = 0,
  THREAD_READY,
  THREAD_RUNNING,
  THREAD_BLOCKED,
  THREAD_BLOCKED_READ_LOCK,
  THREAD_SLEEPING,
  THREAD_SUSPENDED,
  THREAD_DEATH,
};

// Returns a string constant for the given thread state.
const char* ToString(enum thread_state state);

typedef int (*thread_start_routine)(void* arg);
typedef void (*thread_trampoline_routine)() __NO_RETURN;

// clang-format off
#define THREAD_FLAG_DETACHED                 (1 << 0)
#define THREAD_FLAG_FREE_STRUCT              (1 << 1)
#define THREAD_FLAG_REAL_TIME                (1 << 2)
#define THREAD_FLAG_IDLE                     (1 << 3)
#define THREAD_FLAG_NO_BOOST                 (1 << 4)

#define THREAD_SIGNAL_KILL                   (1 << 0)
#define THREAD_SIGNAL_SUSPEND                (1 << 1)
#define THREAD_SIGNAL_POLICY_EXCEPTION       (1 << 2)
// clang-format on

#define THREAD_MAGIC (0x74687264)  // 'thrd'

// This includes the trailing NUL.
// N.B. This must match ZX_MAX_NAME_LEN.
#define THREAD_NAME_LENGTH 32

#define THREAD_LINEBUFFER_LENGTH 128

struct vmm_aspace;

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

void dump_thread_locked(Thread* t, bool full) TA_REQ(thread_lock);
void dump_thread(Thread* t, bool full) TA_EXCL(thread_lock);
void arch_dump_thread(Thread* t);
void dump_all_threads_locked(bool full) TA_REQ(thread_lock);
void dump_all_threads(bool full) TA_EXCL(thread_lock);
void dump_thread_user_tid(uint64_t tid, bool full) TA_EXCL(thread_lock);
void dump_thread_user_tid_locked(uint64_t tid, bool full) TA_REQ(thread_lock);

static inline void dump_thread_during_panic(Thread* t, bool full) TA_NO_THREAD_SAFETY_ANALYSIS {
  // Skip grabbing the lock if we are panic'ing
  dump_thread_locked(t, full);
}

static inline void dump_all_threads_during_panic(bool full) TA_NO_THREAD_SAFETY_ANALYSIS {
  // Skip grabbing the lock if we are panic'ing
  dump_all_threads_locked(full);
}

static inline void dump_thread_user_tid_during_panic(uint64_t tid,
                                                     bool full) TA_NO_THREAD_SAFETY_ANALYSIS {
  // Skip grabbing the lock if we are panic'ing
  dump_thread_user_tid_locked(tid, full);
}

struct Thread {
  // TODO(kulakowski) Are these needed?
  // Default constructor/destructor declared to be not-inline in order to
  // avoid circular include dependencies involving thread, wait_queue, and
  // OwnedWaitQueue.
  Thread();
  ~Thread();

  static Thread* CreateIdleThread(uint cpu_num);
  // Creates a thread with |name| that will execute |entry| at |priority|. |arg|
  // will be passed to |entry| when executed, the return value of |entry| will be
  // passed to Exit().
  // This call allocates a thread and places it in the global thread list. This
  // memory will be freed by either Join() or Detach(), one of these
  // MUST be called.
  // The thread will not be scheduled until Resume() is called.
  static Thread* Create(const char* name, thread_start_routine entry, void* arg, int priority);
  static Thread* CreateEtc(Thread* t, const char* name, thread_start_routine entry, void* arg,
                           int priority, thread_trampoline_routine alt_trampoline);

  void SetCurrent();
  void SetUsermodeThread(ThreadDispatcher* user_thread);
  zx_status_t SetRealTime();

  // Called to mark a thread as schedulable.
  void Resume();
  zx_status_t Suspend();
  void Forget();
  // Marks a thread as detached, in this state its memory will be released once
  // execution is done.
  zx_status_t Detach();
  zx_status_t DetachAndResume();
  // Waits |deadline| time for a thread to complete execution then releases its memory.
  zx_status_t Join(int* retcode, zx_time_t deadline);
  // Deliver a kill signal to a thread.
  void Kill();

  void SetPriority(int priority);
  void SetDeadline(const zx_sched_deadline_params_t& params);

  void* recursive_object_deletion_list() { return recursive_object_deletion_list_; }
  void set_recursive_object_deletion_list(void* ptr) { recursive_object_deletion_list_ = ptr; }

  // Get/set the mask of valid CPUs that thread may run on. If a new mask
  // is set, the thread will be migrated to satisfy the new constraint.
  //
  // Affinity comes in two flavours:
  //
  //   * "hard affinity", which will always be respected by the scheduler.
  //     The scheduler will panic if it can't satisfy this affinity.
  //
  //   * "soft affinity" indicating where the thread should ideally be scheduled.
  //     The scheduler will respect the mask unless there are no other
  //     options (e.g., the soft affinity and hard affinity don't contain
  //     any common CPUs).
  //
  // If the two masks conflict, the hard affinity wins.
  void SetCpuAffinity(cpu_mask_t affinity) TA_EXCL(thread_lock);
  cpu_mask_t GetCpuAffinity() const TA_EXCL(thread_lock);
  void SetSoftCpuAffinity(cpu_mask_t affinity) TA_EXCL(thread_lock);
  cpu_mask_t GetSoftCpuAffinity() const TA_EXCL(thread_lock);

  static Thread* IdToThreadSlow(uint64_t tid);

  void OwnerName(char out_name[THREAD_NAME_LENGTH]);
  // Return the number of nanoseconds a thread has been running for.
  zx_duration_t Runtime() const;
  // Last cpu this thread was running on, or INVALID_CPU if it has never run.
  cpu_num_t LastCpu() const TA_EXCL(thread_lock);
  // Return true if thread has been signaled.
  bool IsSignaled() { return signals_ != 0; }
  bool IsRealtime() const {
    return (flags_ & THREAD_FLAG_REAL_TIME) && base_priority_ > DEFAULT_PRIORITY;
  }
  bool IsIdle() const { return !!(flags_ & THREAD_FLAG_IDLE); }
  bool IsRealTimeOrIdle() const { return !!(flags_ & (THREAD_FLAG_REAL_TIME | THREAD_FLAG_IDLE)); }
  // A thread may not participate in the scheduler's boost behavior if it is...
  //
  // 1) flagged as real-time
  // 2) flagged as idle
  // 3) flagged as "no boost"
  //
  // Note that flag #3 should *only* ever be used by kernel test code when
  // attempting to test priority inheritance chain propagation.  It is important
  // that these tests maintain rigorous control of the relationship between base
  // priority, inherited priority, and the resulting effective priority.  Allowing
  // the scheduler to introduce the concept of dynamic boost priority into the
  // calculation of effective priority makes writing tests like this more
  // difficult which is why we have an internal flag which can be used for
  // disabling this behavior.
  bool CannotBoost() const {
    return !!(flags_ & (THREAD_FLAG_REAL_TIME | THREAD_FLAG_IDLE | THREAD_FLAG_NO_BOOST));
  }

  // All of these operations implicitly operate on the current thread.
  struct Current {
    // This is defined below, just after the Thread declaration.
    static inline Thread* Get();

    // Scheduler routines to be used by regular kernel code.
    static void Yield();
    static void Preempt();
    static void Reschedule();
    static void Exit(int retcode) __NO_RETURN;
    static void BecomeIdle() __NO_RETURN;

    // Wait until the deadline has occurred.
    //
    // If interruptable, may return early with ZX_ERR_INTERNAL_INTR_KILLED if
    // thread is signaled for kill.
    static zx_status_t SleepEtc(const Deadline& deadline, bool interruptable, zx_time_t now);
    // Non-interruptable version of SleepEtc.
    static zx_status_t Sleep(zx_time_t deadline);
    // Non-interruptable relative delay version of Sleep.
    static zx_status_t SleepRelative(zx_duration_t delay);
    // Interruptable version of Sleep.
    static zx_status_t SleepInterruptable(zx_time_t deadline);

    static void SignalPolicyException();

    // Process pending signals, may never return because of kill signal.
    static void ProcessPendingSignals(GeneralRegsSource source, void* gregs);

    // Migrates the current thread to the CPU identified by target_cpu.
    static void MigrateToCpu(cpu_num_t target_cpuid);

    static void SetName(const char* name);

    static void CheckPreemptPending();
    static uint32_t PreemptDisableCount() {
      return Thread::Current::Get()->disable_counts_ & 0xffff;
    }
    static uint32_t ReschedDisableCount() { return Thread::Current::Get()->disable_counts_ >> 16; }
    // PreemptDisable() increments the preempt_disable counter for the
    // current thread.  While preempt_disable is non-zero, preemption of the
    // thread is disabled, including preemption from interrupt handlers.
    // During this time, any call to Reschedule() or sched_reschedule()
    // will only record that a reschedule is pending, and won't do a context
    // switch.
    //
    // Note that this does not disallow blocking operations
    // (e.g. mutex.Acquire()).  Disabling preemption does not prevent switching
    // away from the current thread if it blocks.
    //
    // A call to PreemptDisable() must be matched by a later call to
    // PreemptReenable() to decrement the preempt_disable counter.
    static void PreemptDisable() {
      DEBUG_ASSERT(Thread::Current::PreemptDisableCount() < 0xffff);

      Thread* current_thread = Thread::Current::Get();
      atomic_signal_fence();
      ++current_thread->disable_counts_;
      atomic_signal_fence();
    }
    // PreemptReenable() decrements the preempt_disable counter.  See
    // PreemptDisable().
    static void PreemptReenable() {
      DEBUG_ASSERT(Thread::Current::PreemptDisableCount() > 0);

      Thread* current_thread = Thread::Current::Get();
      atomic_signal_fence();
      uint32_t new_count = --current_thread->disable_counts_;
      atomic_signal_fence();

      if (new_count == 0) {
        DEBUG_ASSERT(!arch_blocking_disallowed());
        Thread::Current::CheckPreemptPending();
      }
    }
    // This is the same as thread_preempt_reenable(), except that it does not
    // check for any pending reschedules.  This is useful in interrupt handlers
    // when we know that no reschedules should have become pending since
    // calling thread_preempt_disable().
    static void PreemptReenableNoResched() {
      DEBUG_ASSERT(Thread::Current::PreemptDisableCount() > 0);

      Thread* current_thread = Thread::Current::Get();
      atomic_signal_fence();
      --current_thread->disable_counts_;
      atomic_signal_fence();
    }
    // ReschedDisable() increments the resched_disable counter for the
    // current thread.  When resched_disable is non-zero, preemption of the
    // thread from outside interrupt handlers is disabled.  However, interrupt
    // handlers may still preempt the thread.
    //
    // This is a weaker version of PreemptDisable().
    //
    // As with PreemptDisable, blocking operations are still allowed while
    // resched_disable is non-zero.
    //
    // A call to ReschedDisable() must be matched by a later call to
    // ReschedReenable() to decrement the preempt_disable counter.
    static void ReschedDisable() {
      DEBUG_ASSERT(Thread::Current::ReschedDisableCount() < 0xffff);

      Thread* current_thread = Thread::Current::Get();
      atomic_signal_fence();
      current_thread->disable_counts_ += 1 << 16;
      atomic_signal_fence();
    }
    // ReschedReenable() decrements the preempt_disable counter.  See
    // ReschedDisable().
    static void ReschedReenable() {
      DEBUG_ASSERT(Thread::Current::ReschedDisableCount() > 0);

      Thread* current_thread = Thread::Current::Get();
      atomic_signal_fence();
      uint32_t new_count = current_thread->disable_counts_ - (1 << 16);
      current_thread->disable_counts_ = new_count;
      atomic_signal_fence();

      if (new_count == 0) {
        DEBUG_ASSERT(!arch_blocking_disallowed());
        Thread::Current::CheckPreemptPending();
      }
    }
    // PreemptSetPending() marks a preemption as pending for the
    // current CPU.
    //
    // This is similar to Reschedule(), except that it may only be
    // used inside an interrupt handler while interrupts and preemption
    // are disabled, between PreemptPisable() and
    // PreemptReenable().  It is similar to sched_reschedule(),
    // except that it does not need to be called with thread_lock held.
    static void PreemptSetPending() {
      DEBUG_ASSERT(arch_ints_disabled());
      DEBUG_ASSERT(arch_blocking_disallowed());
      Thread* current_thread = Thread::Current::Get();
      DEBUG_ASSERT(Thread::Current::PreemptDisableCount() > 0);

      current_thread->preempt_pending_ = true;
    }

    static void PrintCurrentBacktrace();
    // Append the backtrace of the current thread to the passed in char pointer up
    // to `len' characters.
    // Returns the number of chars appended.
    static size_t AppendCurrentBacktrace(char* out, size_t len);
    static void PrintCurrentBacktraceAtFrame(void* caller_frame);

    static void DumpLocked(bool full) TA_REQ(thread_lock);
    static void Dump(bool full) TA_EXCL(thread_lock);
    static void DumpAllThreadsLocked(bool full) TA_REQ(thread_lock);
    static void DumpAllThreads(bool full) TA_EXCL(thread_lock);
    static void DumpUserTid(uint64_t tid, bool full) TA_EXCL(thread_lock);
    static void DumpUserTidLocked(uint64_t tid, bool full) TA_REQ(thread_lock);
    static void DumpAllDuringPanic(bool full) TA_NO_THREAD_SAFETY_ANALYSIS {
      dump_all_threads_during_panic(full);
    }
    static void DumpUserTidDuringPanic(uint64_t tid, bool full) TA_NO_THREAD_SAFETY_ANALYSIS {
      dump_thread_user_tid_during_panic(tid, full);
    }
  };

  // Print the backtrace of the thread, if possible.
  zx_status_t PrintBacktrace();

  void DumpDuringPanic(bool full) TA_NO_THREAD_SAFETY_ANALYSIS {
    dump_thread_during_panic(this, full);
  }

  // TODO: This should all be private. Until migrated away from list_node, we need Thread to be
  // standard layout. For now, OwnedWaitQueue needs to be able to manipulate list_nodes in Thread.
  friend class OwnedWaitQueue;

  int magic_;
  struct list_node thread_list_node_;

  // active bits
  struct list_node queue_node_;
  enum thread_state state_;
  zx_time_t last_started_running_;
  zx_duration_t remaining_time_slice_;
  unsigned int flags_;
  unsigned int signals_;

  // Total time in THREAD_RUNNING state.  If the thread is currently in
  // THREAD_RUNNING state, this excludes the time it has accrued since it
  // left the scheduler.
  zx_duration_t runtime_ns_;

  // priority: in the range of [MIN_PRIORITY, MAX_PRIORITY], from low to high.
  // base_priority is set at creation time, and can be tuned with thread_set_priority().
  // priority_boost is a signed value that is moved around within a range by the scheduler.
  // inherited_priority is temporarily set to >0 when inheriting a priority from another
  // thread blocked on a locking primitive this thread holds. -1 means no inherit.
  // effective_priority is MAX(base_priority + priority boost, inherited_priority) and is
  // the working priority for run queue decisions.
  int effec_priority_;
  int base_priority_;
  int priority_boost_;
  int inherited_priority_;

  SchedulerState scheduler_state_;

  // current cpu the thread is either running on or in the ready queue, undefined otherwise
  cpu_num_t curr_cpu_;
  cpu_num_t last_cpu_;        // last cpu the thread ran on, INVALID_CPU if it's never run
  cpu_mask_t hard_affinity_;  // mask of CPUs this thread _must_ run on
  cpu_mask_t soft_affinity_;  // mask of CPUs this thread should run on if possible

  // if blocked, a pointer to the wait queue
  struct wait_queue* blocking_wait_queue_ TA_GUARDED(thread_lock) = nullptr;

  // a list of the wait queues currently owned by this thread.
  fbl::DoublyLinkedList<OwnedWaitQueue*> owned_wait_queues_ TA_GUARDED(thread_lock);

  // list of other wait queue heads if we're a head
  struct list_node wait_queue_heads_node_;

  // return code if woken up abnormally from suspend, sleep, or block
  zx_status_t blocked_status_;

  // are we allowed to be interrupted on the current thing we're blocked/sleeping on
  bool interruptable_;

#if WITH_LOCK_DEP
  // state for runtime lock validation when in thread context
  lockdep::ThreadLockState lock_state_;
#endif

  // pointer to the kernel address space this thread is associated with
  struct vmm_aspace* aspace_;

  // pointer to user thread if one exists for this thread
  ThreadDispatcher* user_thread_;
  uint64_t user_tid_;
  uint64_t user_pid_;

  // architecture stuff
  struct arch_thread arch_;

  kstack_t stack_;

  // entry point
  thread_start_routine entry_;
  void* arg_;

  // return code
  int retcode_;
  struct wait_queue retcode_wait_queue_;

  // disable_counts_ contains two fields:
  //
  //  * Bottom 16 bits: the preempt_disable counter.  See
  //    PreemptDisable().
  //  * Top 16 bits: the resched_disable counter.  See
  //    ReschedDisable().
  //
  // This is a single field so that both counters can be compared against
  // zero with a single memory access and comparison.
  //
  // disable_counts_ is modified by interrupt handlers, but it is always
  // restored to its original value before the interrupt handler returns,
  // so modifications are not visible to the interrupted thread.  Despite
  // that, "volatile" is still technically needed.  Otherwise the
  // compiler is technically allowed to compile
  // "++thread->disable_counts" into code that stores a junk value into
  // preempt_disable temporarily.
  volatile uint32_t disable_counts_;

  // preempt_pending_ tracks whether a thread reschedule is pending.
  //
  // This is volatile because it can be changed asynchronously by an
  // interrupt handler: If preempt_disable_ is set, an interrupt handler
  // may change this from false to true.  Otherwise, if resched_disable_
  // is set, an interrupt handler may change this from true to false.
  //
  // preempt_pending_ should only be true:
  //  * if preempt_disable_ or resched_disable_ are non-zero, or
  //  * after preempt_disable_ or resched_disable_ have been decremented,
  //    while preempt_pending_ is being checked.
  volatile bool preempt_pending_;

  // This is used by dispatcher.cc:SafeDeleter.
  void* recursive_object_deletion_list_ = nullptr;

  char name_[THREAD_NAME_LENGTH];
#if WITH_DEBUG_LINEBUFFER
  // buffering for debug/klog output
  size_t linebuffer_pos_;
  char linebuffer_[THREAD_LINEBUFFER_LENGTH];
#endif

  // Indicates whether user register state (debug, vector, fp regs, etc.) has been saved to the
  // arch_thread_t as part of thread suspension / exception handling.
  //
  // When a user thread is suspended or generates an exception (synthetic or architectural) that
  // might be observed by another process, we save user register state to the thread's arch_thread_t
  // so that it may be accessed by a debugger.  Upon leaving a suspended or exception state, we
  // restore user register state.
  //
  // See also |thread_is_user_state_saved_locked()| and |ScopedThreadExceptionContext|.
  bool user_state_saved_;
};

// For the moment, the arch-specific current thread implementations need to come here, after the
// Thread definition.
#include <arch/current_thread.h>
Thread* Thread::Current::Get() { return arch_get_current_thread(); }

// TODO(johngro): Remove this when we have addressed fxb/33473.  Right now, this
// is used in only one place (x86_bringup_aps in arch/x86/smp.cpp) outside of
// thread.cpp.
//
// Normal users should only ever need to call either Thread::Create, or
// Thread::CreateEtc.
void init_thread_struct(Thread* t, const char* name);

// Other thread-system bringup functions.
void thread_init_early();
void thread_secondary_cpu_init_early(Thread* t);
void thread_secondary_cpu_entry() __NO_RETURN;
void thread_construct_first(Thread* t, const char* name);

// print the backtrace on the current thread
void thread_print_current_backtrace();

// print the backtrace on the current thread at the given frame
void thread_print_current_backtrace_at_frame(void* caller_frame);

// Call the arch-specific signal handler.
extern "C" void arch_iframe_process_pending_signals(iframe_t* iframe);

// find a thread based on the thread id
// NOTE: used only for debugging, its a slow linear search through the
// global thread list
Thread* thread_id_to_thread_slow(uint64_t tid);

static inline bool thread_lock_held(void) { return spin_lock_held(&thread_lock); }

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
//   Guard<Mutex> al{&lock_};
//   // Do some initial computation...
//   resched_disable.Disable();
//   // Possibly wake another thread...
//
// The AutoReschedDisable must be placed before the Guard to ensure that
// rescheduling is re-enabled only after releasing the mutex.
class AutoReschedDisable {
 public:
  AutoReschedDisable() {}
  ~AutoReschedDisable() {
    if (started_) {
      Thread::Current::ReschedReenable();
    }
  }

  void Disable() {
    if (!started_) {
      Thread::Current::ReschedDisable();
      started_ = true;
    }
  }

  DISALLOW_COPY_ASSIGN_AND_MOVE(AutoReschedDisable);

 private:
  bool started_ = false;
};

// Returns true if |thread|'s user state has been saved.
//
// Caller must hold the thread lock.
bool thread_is_user_state_saved_locked(Thread* thread);

// RAII helper that installs/removes an exception context and saves/restores user register state.
//
// When a thread takes an exception, this class is used to make user register state available to
// debuggers and exception handlers.
//
// Example Usage:
//
// {
//   ScopedThreadExceptionContext context(...);
//   HandleException();
// }
//
// Note, ScopedThreadExceptionContext keeps track of whether the state has already been saved so
// it's safe to nest them:
//
// void Foo() {
//   ScopedThreadExceptionContext context(...);
//   Bar();
// }
//
// void Bar() {
//   ScopedThreadExceptionContext context(...);
//   Baz();
// }
//
class ScopedThreadExceptionContext {
 public:
  // |thread| must be the calling thread.
  ScopedThreadExceptionContext(Thread* thread, const arch_exception_context_t* context);
  ~ScopedThreadExceptionContext();
  DISALLOW_COPY_ASSIGN_AND_MOVE(ScopedThreadExceptionContext);

 private:
  Thread* thread_;
  const arch_exception_context_t* context_;
  bool need_to_remove_;
  bool need_to_restore_;
};

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_THREAD_H_
