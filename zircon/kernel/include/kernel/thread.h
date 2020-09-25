// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_THREAD_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_THREAD_H_

#include <debug.h>
#include <lib/io.h>
#include <platform.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/listnode.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/scheduler.h>
#include <zircon/types.h>

#include <arch/defines.h>
#include <arch/exception.h>
#include <arch/ops.h>
#include <arch/thread.h>
#include <fbl/canary.h>
#include <fbl/function.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>
#include <kernel/cpu.h>
#include <kernel/deadline.h>
#include <kernel/scheduler_state.h>
#include <kernel/spinlock.h>
#include <kernel/task_runtime_stats.h>
#include <kernel/thread_lock.h>
#include <kernel/timer.h>
#include <ktl/atomic.h>
#include <ktl/string_view.h>
#include <lockdep/thread_lock_state.h>
#include <vm/kstack.h>

class Dpc;
struct Thread;
class OwnedWaitQueue;
class ThreadDispatcher;
class WaitQueue;
class VmAspace;

// These forward declarations are needed so that Thread can friend
// them before they are defined.
static inline Thread* arch_get_current_thread();
static inline void arch_set_current_thread(Thread*);

// When blocking this enum indicates the kind of resource ownership that is being waited for
// that is causing the block.
enum class ResourceOwnership {
  // Blocking is either not for any particular resource, or it is to wait for
  // exclusive access to a resource.
  Normal,
  // Blocking is happening whilst waiting for shared read access to a resource.
  Reader,
};

// Whether a block or a sleep can be interrupted.
enum class Interruptible : bool { No, Yes };

// When signaling to a wait queue that the priority of one of its blocked
// threads has changed, this enum is used as a signal indicating whether or not
// the priority change should be propagated down the PI chain (if any) or not.
enum class PropagatePI : bool { No = false, Yes };

// A trait for Threads that are the head of a wait queue sublist.
struct WaitQueueHeadsTrait {
  using NodeState = fbl::DoublyLinkedListNodeState<Thread*>;
  static NodeState& node_state(Thread& thread);
};
using WaitQueueHeads = fbl::DoublyLinkedListCustomTraits<Thread*, WaitQueueHeadsTrait>;

// A trait for Threads on a wait queue sublist.
//
// Threads can be removed from a sublist without knowing which sublist they are on.
struct WaitQueueSublistTrait {
  using NodeState =
      fbl::DoublyLinkedListNodeState<Thread*, fbl::NodeOptions::AllowRemoveFromContainer>;
  static NodeState& node_state(Thread& thread);
};
using WaitQueueSublist = fbl::DoublyLinkedListCustomTraits<Thread*, WaitQueueSublistTrait>;

// Encapsulation of all the per-thread state for the wait queue data structure.
class WaitQueueState {
 public:
  WaitQueueState() = default;

  ~WaitQueueState();

  // Disallow copying.
  WaitQueueState(const WaitQueueState&) = delete;
  WaitQueueState& operator=(const WaitQueueState&) = delete;

  bool IsHead() const { return heads_node_.InContainer(); }
  bool InWaitQueue() const { return IsHead() || sublist_node_.InContainer(); }

  zx_status_t BlockedStatus() const TA_REQ(thread_lock) { return blocked_status_; }

  void Block(Interruptible interruptible, zx_status_t status) TA_REQ(thread_lock);

  void UnblockIfInterruptible(Thread* thread, zx_status_t status) TA_REQ(thread_lock);

  // Returns whether a reschedule needs to be performed.
  bool Unsleep(Thread* thread, zx_status_t status) TA_REQ(thread_lock);
  bool UnsleepIfInterruptible(Thread* thread, zx_status_t status) TA_REQ(thread_lock);

  void UpdatePriorityIfBlocked(Thread* thread, int priority, PropagatePI propagate)
      TA_REQ(thread_lock);

  void AssertNoOwnedWaitQueues() const TA_REQ(thread_lock) {
    DEBUG_ASSERT(owned_wait_queues_.is_empty());
  }

  void AssertNotBlocked() const TA_REQ(thread_lock) {
    DEBUG_ASSERT(blocking_wait_queue_ == nullptr);
    DEBUG_ASSERT(!InWaitQueue());
  }

 private:
  // WaitQueues, WaitQueueCollections, and their List types, can
  // directly manipulate the contents of the per-thread state, for now.
  friend class OwnedWaitQueue;
  friend class WaitQueue;
  friend class WaitQueueCollection;
  friend struct WaitQueueHeadsTrait;
  friend struct WaitQueueSublistTrait;

  // If blocked, a pointer to the WaitQueue the Thread is on.
  WaitQueue* blocking_wait_queue_ TA_GUARDED(thread_lock) = nullptr;

  // A list of the WaitQueues currently owned by this Thread.
  fbl::DoublyLinkedList<OwnedWaitQueue*> owned_wait_queues_ TA_GUARDED(thread_lock);

  // Any given thread is either a WaitQueue head (in which case
  // sublist_ is in use, and may be non-empty), or not (in which case
  // sublist_node_ is used).

  // The Thread's position in a WaitQueue sublist. If active, this
  // Thread is under some queue head (another Thread of the same
  // priority).
  //
  // This storage is also used for Scheduler::Unblock()ing multiple
  // Threads from a WaitQueue at once.
  WaitQueueSublistTrait::NodeState sublist_node_;

  // The Thread's sublist. This is only used when the Thread is a
  // WaitQueue head (and so, when IsHead() is true).
  WaitQueueSublist sublist_;

  // The Thread's position in a WaitQueue heads list. If active, this
  // Thread is a WaitQueue head (and so, IsHead() is true).
  WaitQueueHeadsTrait::NodeState heads_node_;

  // Return code if woken up abnormally from suspend, sleep, or block.
  zx_status_t blocked_status_ = ZX_OK;

  // Dumping routines are allowed to see inside us.
  friend void dump_thread_locked(Thread* t, bool full_dump);

  // Are we allowed to be interrupted on the current thing we're blocked/sleeping on?
  Interruptible interruptible_ = Interruptible::No;
};

// Encapsulation of the data structure backing the wait queue.
//
// This maintains an ordered collection of Threads.
//
// All such collections are protected by the thread_lock.
class WaitQueueCollection {
 public:
  constexpr WaitQueueCollection() {}

  // The number of threads currently in the collection.
  uint32_t Count() const TA_REQ(thread_lock) { return count_; }

  // Peek at the first Thread in the collection.
  Thread* Peek() TA_REQ(thread_lock);
  const Thread* Peek() const TA_REQ(thread_lock);

  // Add the Thread into its sorted location in the collection.
  void Insert(Thread* thread) TA_REQ(thread_lock);

  // Remove the Thread from the collection.
  void Remove(Thread* thread) TA_REQ(thread_lock);

  // This function enumerates the collection in a fashion which allows us to
  // remove the threads in question as they are presented to our injected
  // function for consideration.
  //
  // Callable should be a lambda which takes a Thread* for consideration and
  // returns a bool.  If it returns true, iteration continues, otherwise it
  // immediately stops.
  //
  // Because this needs to see Thread internals, it is declared here and
  // defined after the Thread definition in thread.h.
  template <typename Callable>
  void ForeachThread(const Callable& visit_thread) TA_REQ(thread_lock);

  // When WAIT_QUEUE_VALIDATION is set, many wait queue operations check that the internals of this
  // data structure are correct, via this method.
  void Validate() const TA_REQ(thread_lock);

  // Disallow copying.
  WaitQueueCollection(const WaitQueueCollection&) = delete;
  WaitQueueCollection& operator=(const WaitQueueCollection&) = delete;

 private:
  int count_ = 0;
  WaitQueueHeads heads_;
};

// NOTE: must be inside critical section when using these
class WaitQueue {
 public:
  constexpr WaitQueue() : WaitQueue(kMagic) {}
  ~WaitQueue();

  WaitQueue(WaitQueue&) = delete;
  WaitQueue(WaitQueue&&) = delete;
  WaitQueue& operator=(WaitQueue&) = delete;
  WaitQueue& operator=(WaitQueue&&) = delete;

  // Remove a specific thread out of a wait queue it's blocked on.
  static zx_status_t UnblockThread(Thread* t, zx_status_t wait_queue_error) TA_REQ(thread_lock);

  // Block on a wait queue.
  // The returned status is whatever the caller of WaitQueue::Wake_*() specifies.
  // A deadline other than Deadline::infinite() will abort at the specified time
  // and return ZX_ERR_TIMED_OUT. A deadline in the past will immediately return.
  zx_status_t Block(const Deadline& deadline, Interruptible interruptible) TA_REQ(thread_lock) {
    return BlockEtc(deadline, 0, ResourceOwnership::Normal, interruptible);
  }

  // Block on a wait queue with a zx_time_t-typed deadline.
  zx_status_t Block(zx_time_t deadline, Interruptible interruptible) TA_REQ(thread_lock) {
    return BlockEtc(Deadline::no_slack(deadline), 0, ResourceOwnership::Normal, interruptible);
  }

  // Block on a wait queue, ignoring existing signals in |signal_mask|.
  // The returned status is whatever the caller of WaitQueue::Wake_*() specifies, or
  // ZX_ERR_TIMED_OUT if the deadline has elapsed or is in the past.
  // This will never timeout when called with a deadline of Deadline::infinite().
  zx_status_t BlockEtc(const Deadline& deadline, uint signal_mask, ResourceOwnership reason,
                       Interruptible interruptible) TA_REQ(thread_lock);

  // Returns the current highest priority blocked thread on this wait queue, or
  // nullptr if no threads are blocked.
  Thread* Peek() TA_REQ(thread_lock);
  const Thread* Peek() const TA_REQ(thread_lock);

  // Release one or more threads from the wait queue.
  // reschedule = should the system reschedule if any is released.
  // wait_queue_error = what WaitQueue::Block() should return for the blocking thread.
  //
  // Returns true if a thread was woken, and false otherwise.
  bool WakeOne(bool reschedule, zx_status_t wait_queue_error) TA_REQ(thread_lock);

  void WakeAll(bool reschedule, zx_status_t wait_queue_error) TA_REQ(thread_lock);

  // Whether the wait queue is currently empty.
  bool IsEmpty() const TA_REQ(thread_lock);

  uint32_t Count() const TA_REQ(thread_lock) { return collection_.Count(); }

  // Returns the highest priority of all the blocked threads on this WaitQueue.
  // Returns -1 if no threads are blocked.
  int BlockedPriority() const TA_REQ(thread_lock);

  // Used by WaitQueue and OwnedWaitQueue to manage changes to the maximum
  // priority of a wait queue due to external effects (thread priority change,
  // thread timeout, thread killed).
  bool UpdatePriority(int old_prio) TA_REQ(thread_lock);

  // A thread's priority has changed.  Update the wait queue bookkeeping to
  // properly reflect this change.
  //
  // |t| must be blocked on this WaitQueue.
  //
  // If |propagate| is PropagatePI::Yes, call into the wait queue code to
  // propagate the priority change down the PI chain (if any).  Then returns true
  // if the change of priority has affected the priority of another thread due to
  // priority inheritance, or false otherwise.
  //
  // If |propagate| is PropagatePI::No, do not attempt to propagate the PI change.
  // This is the mode used by OwnedWaitQueue during a batch update of a PI chain.
  bool PriorityChanged(Thread* t, int old_prio, PropagatePI propagate) TA_REQ(thread_lock);

  // OwnedWaitQueue needs to be able to call this on WaitQueues to
  // determine if they are base WaitQueues or the OwnedWaitQueue
  // subclass.
  uint32_t magic() const { return magic_; }

 protected:
  explicit constexpr WaitQueue(uint32_t magic) : magic_(magic) {}

  // Inline helpers (defined in wait_queue_internal.h) for
  // WaitQueue::BlockEtc and OwnedWaitQueue::BlockAndAssignOwner to
  // share.
  inline zx_status_t BlockEtcPreamble(const Deadline& deadline, uint signal_mask,
                                      ResourceOwnership reason, Interruptible interuptible)
      TA_REQ(thread_lock);
  inline zx_status_t BlockEtcPostamble(const Deadline& deadline) TA_REQ(thread_lock);

  // Dequeue the specified thread and set its blocked_status.  Do not actually
  // schedule the thread to run.
  void DequeueThread(Thread* t, zx_status_t wait_queue_error) TA_REQ(thread_lock);

  // Move the specified thread from the source wait queue to the dest wait queue.
  static void MoveThread(WaitQueue* source, WaitQueue* dest, Thread* t) TA_REQ(thread_lock);

 private:
  // Dequeue the first waiting thread, and set its blocking status, then return a
  // pointer to the thread which was dequeued.  Do not actually schedule the
  // thread to run.
  Thread* DequeueOne(zx_status_t wait_queue_error) TA_REQ(thread_lock);

  static void TimeoutHandler(Timer* timer, zx_time_t now, void* arg);

  // Internal helper for dequeueing a single Thread.
  void Dequeue(Thread* t, zx_status_t wait_queue_error) TA_REQ(thread_lock);

  // Validate that the queue of a given WaitQueue is valid.
  void ValidateQueue() TA_REQ(thread_lock);

  // Note: Wait queues come in 2 flavors (traditional and owned) which are
  // distinguished using the magic number.  The point here is that, unlike
  // most other magic numbers in the system, the wait_queue_t serves a
  // functional purpose beyond checking for corruption debug builds.
  static constexpr uint32_t kMagic = fbl::magic("wait");
  uint32_t magic_;

  // The OwnedWaitQueue subclass also manipulates the collection.
 protected:
  WaitQueueCollection collection_;
};

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
#define THREAD_FLAG_IDLE                     (1 << 2)
#define THREAD_FLAG_VCPU                     (1 << 3)

#define THREAD_SIGNAL_KILL                   (1 << 0)
#define THREAD_SIGNAL_SUSPEND                (1 << 1)
#define THREAD_SIGNAL_POLICY_EXCEPTION       (1 << 2)
// clang-format on

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
void dump_thread_user_tid(zx_koid_t tid, bool full) TA_EXCL(thread_lock);
void dump_thread_user_tid_locked(zx_koid_t tid, bool full) TA_REQ(thread_lock);

static inline void dump_thread_during_panic(Thread* t, bool full) TA_NO_THREAD_SAFETY_ANALYSIS {
  // Skip grabbing the lock if we are panic'ing
  dump_thread_locked(t, full);
}

static inline void dump_all_threads_during_panic(bool full) TA_NO_THREAD_SAFETY_ANALYSIS {
  // Skip grabbing the lock if we are panic'ing
  dump_all_threads_locked(full);
}

static inline void dump_thread_user_tid_during_panic(zx_koid_t tid,
                                                     bool full) TA_NO_THREAD_SAFETY_ANALYSIS {
  // Skip grabbing the lock if we are panic'ing
  dump_thread_user_tid_locked(tid, full);
}

class PreemptionState {
 public:
  volatile bool& preempt_pending() { return preempt_pending_; }
  const volatile bool& preempt_pending() const { return preempt_pending_; }

  void CheckPreemptPending() const;

  bool PreemptOrReschedDisabled() const {
    return PreemptDisableCount() > 0 || ReschedDisableCount() > 0;
  }

  uint32_t PreemptDisableCount() const { return disable_counts_ & 0xffff; }

  uint32_t ReschedDisableCount() const { return disable_counts_ >> 16; }

  // PreemptDisable() increments the preempt_disable counter for the
  // current thread.  While preempt_disable is non-zero, preemption of the
  // thread is disabled, including preemption from interrupt handlers.
  // During this time, any call to Reschedule()
  // will only record that a reschedule is pending, and won't do a context
  // switch.
  //
  // Note that this does not disallow blocking operations
  // (e.g. mutex.Acquire()).  Disabling preemption does not prevent switching
  // away from the current thread if it blocks.
  //
  // A call to PreemptDisable() must be matched by a later call to
  // PreemptReenable() to decrement the preempt_disable counter.
  void PreemptDisable() {
    DEBUG_ASSERT(PreemptDisableCount() < 0xffff);

    ktl::atomic_signal_fence(ktl::memory_order_seq_cst);
    ++disable_counts_;
    ktl::atomic_signal_fence(ktl::memory_order_seq_cst);
  }

  // PreemptReenable() decrements the preempt_disable counter.  See
  // PreemptDisable().
  void PreemptReenable() {
    DEBUG_ASSERT(PreemptDisableCount() > 0);

    ktl::atomic_signal_fence(ktl::memory_order_seq_cst);
    uint32_t new_count = --disable_counts_;
    ktl::atomic_signal_fence(ktl::memory_order_seq_cst);

    if (new_count == 0) {
      DEBUG_ASSERT(!arch_blocking_disallowed());
      CheckPreemptPending();
    }
  }

  // This is the same as thread_preempt_reenable(), except that it does not
  // check for any pending reschedules.  This is useful in interrupt handlers
  // when we know that no reschedules should have become pending since
  // calling thread_preempt_disable().
  void PreemptReenableNoResched() {
    DEBUG_ASSERT(PreemptDisableCount() > 0);

    ktl::atomic_signal_fence(ktl::memory_order_seq_cst);
    --disable_counts_;
    ktl::atomic_signal_fence(ktl::memory_order_seq_cst);
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
  void ReschedDisable() {
    DEBUG_ASSERT(ReschedDisableCount() < 0xffff);

    ktl::atomic_signal_fence(ktl::memory_order_seq_cst);
    disable_counts_ += 1 << 16;
    ktl::atomic_signal_fence(ktl::memory_order_seq_cst);
  }

  // ReschedReenable() decrements the preempt_disable counter.  See
  // ReschedDisable().
  void ReschedReenable() {
    DEBUG_ASSERT(ReschedDisableCount() > 0);

    ktl::atomic_signal_fence(ktl::memory_order_seq_cst);
    uint32_t new_count = disable_counts_ - (1 << 16);
    disable_counts_ = new_count;
    ktl::atomic_signal_fence(ktl::memory_order_seq_cst);

    if (new_count == 0) {
      DEBUG_ASSERT(!arch_blocking_disallowed());
      CheckPreemptPending();
    }
  }

  // PreemptSetPending() marks a preemption as pending for the
  // current CPU.
  //
  // This is similar to Reschedule(), except that it may only be
  // used inside an interrupt handler while interrupts and preemption
  // are disabled, between PreemptPisable() and
  // PreemptReenable().  It is similar to Scheduler::Reschedule(),
  // except that it does not need to be called with thread_lock held.
  void PreemptSetPending() {
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(arch_blocking_disallowed());
    DEBUG_ASSERT(PreemptDisableCount() > 0);

    preempt_pending_ = true;
  }

 private:
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
};

// TaskState is responsible for running the task defined by
// |entry(arg)|, and reporting its value to any joining threads.
//
// TODO: the detached state in Thread::flags_ probably belongs here.
class TaskState {
 public:
  TaskState() = default;

  void Init(thread_start_routine entry, void* arg);

  zx_status_t Join(zx_time_t deadline) TA_REQ(thread_lock);

  void WakeJoiners(zx_status_t status) TA_REQ(thread_lock);

  thread_start_routine entry() { return entry_; }
  void* arg() { return arg_; }

  int retcode() { return retcode_; }
  void set_retcode(int retcode) { retcode_ = retcode; }

 private:
  // Dumping routines are allowed to see inside us.
  friend void dump_thread_locked(Thread* t, bool full_dump);

  // The Thread's entry point, and its argument.
  thread_start_routine entry_ = nullptr;
  void* arg_ = nullptr;

  // Storage for the return code.
  int retcode_ = 0;

  // Other threads waiting to join this Thread.
  WaitQueue retcode_wait_queue_;
};

// Keeps track of whether a thread is allowed to allocate memory.
//
// A thread's |MemoryAllocationState| should only be accessed by that thread itself or interrupt
// handlers running in the thread's context.
class MemoryAllocationState {
 public:
  void Disable() {
    ktl::atomic_signal_fence(ktl::memory_order_seq_cst);
    disable_count_++;
    ktl::atomic_signal_fence(ktl::memory_order_seq_cst);
  }

  void Enable() {
    ktl::atomic_signal_fence(ktl::memory_order_seq_cst);
    DEBUG_ASSERT(disable_count_ > 0);
    disable_count_--;
    ktl::atomic_signal_fence(ktl::memory_order_seq_cst);
  }

  // Returns true if memory allocation is allowed.
  bool IsEnabled() {
    ktl::atomic_signal_fence(ktl::memory_order_seq_cst);
    return disable_count_ == 0;
  }

 private:
  // Notice that we aren't using atomic operations to access the field.  We don't need atomic
  // operations here as long as...
  //
  // 1. We use atomic_signal_fence to prevent compiler reordering.
  //
  // 2. We use volatile to ensure the compiler actually generates loads and stores for the value (so
  // the interrupt handler can see what the thread see, and vice versa).
  //
  // 3. Upon completion, an interrupt handler that modified the field restores it to the value it
  // held at the start of the interrupt.
  volatile uint32_t disable_count_ = 0;
};

struct Thread {
  // TODO(kulakowski) Are these needed?
  // Default constructor/destructor declared to be not-inline in order to
  // avoid circular include dependencies involving Thread, WaitQueue, and
  // OwnedWaitQueue.
  Thread();
  ~Thread();

  static Thread* CreateIdleThread(cpu_num_t cpu_num);
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

  // Internal initialization routines. Eventually, these should be private.
  void SecondaryCpuInitEarly();

  // Associate this Thread to the given ThreadDispatcher.
  void SetUsermodeThread(fbl::RefPtr<ThreadDispatcher> user_thread);

  // Get the associated ThreadDispatcher.
  ThreadDispatcher* user_thread() { return user_thread_.get(); }
  const ThreadDispatcher* user_thread() const { return user_thread_.get(); }

  // Get the koid of the associated ThreadDisptacher or its containing ProcessDispatcher.
  zx_koid_t user_pid() const { return user_pid_; }
  zx_koid_t user_tid() const { return user_tid_; }

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

  // Erase this thread from all global lists, where applicable.
  void EraseFromListsLocked() TA_REQ(thread_lock);

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

  enum class MigrateStage {
    // The stage before the thread has migrated. Called from the old CPU.
    Before,
    // The stage after the thread has migrated. Called from the new CPU.
    After,
    // The Thread is exiting. Can be called from any CPU.
    Exiting,
  };
  // The migrate function will be invoked twice when a thread is migrate between
  // CPUs. Firstly when the thread is removed from the old CPUs scheduler,
  // secondly when the thread is rescheduled on the new CPU. When the migrate
  // function is called, |thread_lock| is held.
  using MigrateFn = fbl::Function<void(Thread* thread, MigrateStage stage)> TA_REQ(thread_lock);

  void SetMigrateFn(MigrateFn migrate_fn) TA_EXCL(thread_lock);
  void SetMigrateFnLocked(MigrateFn migrate_fn) TA_REQ(thread_lock);

  void CallMigrateFnLocked(MigrateStage stage) TA_REQ(thread_lock) {
    if (unlikely(migrate_fn_)) {
      migrate_fn_(this, stage);
    }
  }

  // Call |migrate_fn| for each thread that was last run on the current CPU.
  static void CallMigrateFnForCpuLocked(cpu_num_t cpu) TA_REQ(thread_lock);

  void OwnerName(char out_name[ZX_MAX_NAME_LEN]);
  // Return the number of nanoseconds a thread has been running for.
  zx_duration_t Runtime() const;

  // Last cpu this thread was running on, or INVALID_CPU if it has never run.
  cpu_num_t LastCpu() const TA_EXCL(thread_lock);
  cpu_num_t LastCpuLocked() const;

  // Return true if thread has been signaled.
  bool IsSignaled() { return signals_ != 0; }
  bool IsIdle() const { return !!(flags_ & THREAD_FLAG_IDLE); }

  // Returns true if this Thread's user state has been saved.
  //
  // Caller must hold the thread lock.
  bool IsUserStateSavedLocked() const TA_REQ(thread_lock) {
    DEBUG_ASSERT(thread_lock.IsHeld());
    return user_state_saved_;
  }

  // Callback for the Timer used for SleepEtc.
  static void SleepHandler(Timer* timer, zx_time_t now, void* arg);
  void HandleSleep(Timer* timer, zx_time_t now);

  // All of these operations implicitly operate on the current thread.
  struct Current {
    // This is defined below, just after the Thread declaration.
    static inline Thread* Get();

    // Scheduler routines to be used by regular kernel code.
    static void Yield();
    static void Preempt();
    static void Reschedule();
    static void Exit(int retcode) __NO_RETURN;
    static void ExitLocked(int retcode) TA_REQ(thread_lock) __NO_RETURN;
    static void BecomeIdle() __NO_RETURN;

    // Wait until the deadline has occurred.
    //
    // If interruptible, may return early with ZX_ERR_INTERNAL_INTR_KILLED if
    // thread is signaled for kill.
    static zx_status_t SleepEtc(const Deadline& deadline, Interruptible interruptible,
                                zx_time_t now);
    // Non-interruptible version of SleepEtc.
    static zx_status_t Sleep(zx_time_t deadline);
    // Non-interruptible relative delay version of Sleep.
    static zx_status_t SleepRelative(zx_duration_t delay);
    // Interruptible version of Sleep.
    static zx_status_t SleepInterruptible(zx_time_t deadline);

    // Transition the current thread to the THREAD_SUSPENDED state.
    static void DoSuspend();

    static void SignalPolicyException();

    // Process pending signals, may never return because of kill signal.
    static void ProcessPendingSignals(GeneralRegsSource source, void* gregs);

    // Migrates the current thread to the CPU identified by target_cpu.
    static void MigrateToCpu(cpu_num_t target_cpuid);

    static void SetName(const char* name);

    static PreemptionState& preemption_state() {
      return Thread::Current::Get()->preemption_state();
    }

    static MemoryAllocationState& memory_allocation_state() {
      return Thread::Current::Get()->memory_allocation_state_;
    }

    // Print the backtrace on the current thread
    static void PrintBacktrace();

    // Print the backtrace on the current thread at the given frame.
    static void PrintBacktraceAtFrame(void* caller_frame);

    // Append the backtrace of the current thread to the passed in char pointer up
    // to `len' characters.
    // Returns the number of chars appended.
    static size_t AppendBacktrace(char* out, size_t len);

    static void DumpLocked(bool full) TA_REQ(thread_lock);
    static void Dump(bool full) TA_EXCL(thread_lock);
    static void DumpAllThreadsLocked(bool full) TA_REQ(thread_lock);
    static void DumpAllThreads(bool full) TA_EXCL(thread_lock);
    static void DumpUserTid(zx_koid_t tid, bool full) TA_EXCL(thread_lock);
    static void DumpUserTidLocked(zx_koid_t tid, bool full) TA_REQ(thread_lock);
    static void DumpAllDuringPanic(bool full) TA_NO_THREAD_SAFETY_ANALYSIS {
      dump_all_threads_during_panic(full);
    }
    static void DumpUserTidDuringPanic(zx_koid_t tid, bool full) TA_NO_THREAD_SAFETY_ANALYSIS {
      dump_thread_user_tid_during_panic(tid, full);
    }
  };

  // Trait for the global Thread list.
  struct ThreadListTrait {
    static fbl::DoublyLinkedListNodeState<Thread*>& node_state(Thread& thread) {
      return thread.thread_list_node_;
    }
  };
  using List = fbl::DoublyLinkedListCustomTraits<Thread*, ThreadListTrait>;

  // Stats for a thread's runtime.
  struct RuntimeStats {
    TaskRuntimeStats runtime;

    // The last state the thread entered.
    thread_state state = thread_state::THREAD_INITIAL;

    // The time at which the thread last entered the state.
    zx_time_t state_time = 0;

    // Update this runtime stat with newer content.
    //
    // Adds to CPU and queue time, but sets the given state directly.
    void Update(const RuntimeStats& other) {
      runtime.Add(other.runtime);
      state = other.state;
      state_time = other.state_time;
    }

    // Get the current TaskRuntimeStats, including the current scheduler state.
    TaskRuntimeStats TotalRuntime() const {
      TaskRuntimeStats ret = runtime;
      if (state == thread_state::THREAD_RUNNING) {
        ret.cpu_time = zx_duration_add_duration(
            ret.cpu_time, zx_duration_sub_duration(current_time(), state_time));
      } else if (state == thread_state::THREAD_READY) {
        ret.queue_time = zx_duration_add_duration(
            ret.queue_time, zx_duration_sub_duration(current_time(), state_time));
      }
      return ret;
    }

    // Adds the local stats to the given output for userspace.
    //
    // This method uses the current state of the thread to include partial runtime and queue time
    // between reschedules.
    void AccumulateRuntimeTo(zx_info_task_runtime_t* info) const {
      TaskRuntimeStats runtime = TotalRuntime();
      runtime.AccumulateRuntimeTo(info);
    }
  };

  void UpdateRuntimeStats(const RuntimeStats& stats) TA_REQ(thread_lock);

  // Print the backtrace of the thread, if possible.
  zx_status_t PrintBacktrace();

  void DumpDuringPanic(bool full) TA_NO_THREAD_SAFETY_ANALYSIS {
    dump_thread_during_panic(this, full);
  }

  // Accessors into Thread state. When the conversion to all-private
  // members is complete (bug 54383), we can revisit the overall
  // Thread API.

  thread_state state() const { return state_; }

  // The scheduler can set threads to be running, or to be ready to run.
  void set_running() { state_ = THREAD_RUNNING; }
  void set_ready() { state_ = THREAD_READY; }
  // While wait queues can set threads to be blocked.
  void set_blocked() { state_ = THREAD_BLOCKED; }
  void set_blocked_read_lock() { state_ = THREAD_BLOCKED_READ_LOCK; }

  // Accessors for specific flags_ bits.
  bool detatched() const { return (flags_ & THREAD_FLAG_DETACHED) != 0; }
  void set_detached(bool value) {
    if (value) {
      flags_ |= THREAD_FLAG_DETACHED;
    } else {
      flags_ &= ~THREAD_FLAG_DETACHED;
    }
  }
  bool free_struct() const { return (flags_ & THREAD_FLAG_FREE_STRUCT) != 0; }
  void set_free_struct(bool value) {
    if (value) {
      flags_ |= THREAD_FLAG_FREE_STRUCT;
    } else {
      flags_ &= ~THREAD_FLAG_FREE_STRUCT;
    }
  }
  bool idle() const { return (flags_ & THREAD_FLAG_IDLE) != 0; }
  void set_idle(bool value) {
    if (value) {
      flags_ |= THREAD_FLAG_IDLE;
    } else {
      flags_ &= ~THREAD_FLAG_IDLE;
    }
  }
  bool vcpu() const { return (flags_ & THREAD_FLAG_VCPU) != 0; }
  void set_vcpu(bool value) {
    if (value) {
      flags_ |= THREAD_FLAG_VCPU;
    } else {
      flags_ &= ~THREAD_FLAG_VCPU;
    }
  }

  // Access to the entire flags_ value, for diagnostics.
  unsigned int flags() const { return flags_; }

  unsigned int signals() const { return signals_; }

  bool has_migrate_fn() const { return migrate_fn_ != nullptr; }

  TaskState& task_state() { return task_state_; }
  const TaskState& task_state() const { return task_state_; }

  PreemptionState& preemption_state() { return preemption_state_; }
  const PreemptionState& preemption_state() const { return preemption_state_; }

  SchedulerState& scheduler_state() { return scheduler_state_; }
  const SchedulerState& scheduler_state() const { return scheduler_state_; }

  WaitQueueState& wait_queue_state() { return wait_queue_state_; }
  const WaitQueueState& wait_queue_state() const { return wait_queue_state_; }

#if WITH_LOCK_DEP
  lockdep::ThreadLockState& lock_state() { return lock_state_; }
  const lockdep::ThreadLockState& lock_state() const { return lock_state_; }
#endif

  arch_thread& arch() { return arch_; }

  KernelStack& stack() { return stack_; }
  const KernelStack& stack() const { return stack_; }

  VmAspace* aspace() { return aspace_; }
  const VmAspace* aspace() const { return aspace_; }
  VmAspace* switch_aspace(VmAspace* aspace) {
    VmAspace* old_aspace = aspace_;
    aspace_ = aspace;
    return old_aspace;
  }

  const char* name() const { return name_; }
  // This may truncate |name|, so that it (including a trailing NUL
  // byte) fit in ZX_MAX_NAME_LEN bytes.
  void set_name(ktl::string_view name);

  Linebuffer& linebuffer() { return linebuffer_; }

  using Canary = fbl::Canary<fbl::magic("thrd")>;
  const Canary& canary() const { return canary_; }

 private:
  // The architecture-specific methods for getting and setting the
  // current thread may need to see Thread's arch_ member via offsetof.
  friend inline Thread* arch_get_current_thread();
  friend inline void arch_set_current_thread(Thread*);

  // OwnedWaitQueues manipulate wait queue state.
  friend class OwnedWaitQueue;

  // ScopedThreadExceptionContext is the only public way to call
  // SaveUserStateLocked and RestoreUserStateLocked.
  friend class ScopedThreadExceptionContext;

  // Dumping routines are allowed to see inside us.
  friend void dump_thread_locked(Thread* t, bool full_dump);

  // The default trampoline used when running the Thread. This can be
  // replaced by the |alt_trampoline| parameter to CreateEtc().
  static void Trampoline() TA_REQ(thread_lock) __NO_RETURN;

  // Dpc callback used for cleaning up a detached Thread's resources.
  static void FreeDpc(Dpc* dpc);

  // Save the arch-specific user state.
  //
  // Returns true when the user state will later need to be restored.
  [[nodiscard]] bool SaveUserStateLocked() TA_REQ(thread_lock);

  // Restore the arch-specific user state.
  void RestoreUserStateLocked() TA_REQ(thread_lock);

  // Returns true if it decides to kill the thread, which must be the
  // current thread. The thread_lock must be held when calling this
  // function.
  //
  // TODO: move this to CurrentThread, once that becomes a subclass of
  // Thread.
  bool CheckKillSignal() TA_REQ(thread_lock);

  __NO_RETURN void ExitLocked(int retcode) TA_REQ(thread_lock);

 private:
  Canary canary_;

  fbl::DoublyLinkedListNodeState<Thread*> thread_list_node_ TA_GUARDED(thread_lock);

  // active bits
  enum thread_state state_;
  unsigned int flags_;
  unsigned int signals_;

  SchedulerState scheduler_state_;

  WaitQueueState wait_queue_state_;

#if WITH_LOCK_DEP
  // state for runtime lock validation when in thread context
  lockdep::ThreadLockState lock_state_;
#endif

  // pointer to the kernel address space this thread is associated with
  VmAspace* aspace_;

  // Strong reference to user thread if one exists for this thread.
  // In the common case freeing Thread will also free ThreadDispatcher when this
  // reference is dropped.
  fbl::RefPtr<ThreadDispatcher> user_thread_;

  // When user_thread_ is set, these are the koids of the
  // ThreadDispatcher and its parent ProcessDispatcher. We cache them
  // here since their values may be used even after those Dispatchers
  // are torn down.
  zx_koid_t user_tid_;
  zx_koid_t user_pid_;

  // Architecture-specific stuff.
  struct arch_thread arch_;

  KernelStack stack_;

  TaskState task_state_;

  PreemptionState preemption_state_;

  MemoryAllocationState memory_allocation_state_;

  // This is used by dispatcher.cc:SafeDeleter.
  void* recursive_object_deletion_list_ = nullptr;

  // This always includes the trailing NUL.
  char name_[ZX_MAX_NAME_LEN];

  // Buffering for Debuglog output.
  Linebuffer linebuffer_;

  // Indicates whether user register state (debug, vector, fp regs, etc.) has been saved to the
  // arch_thread_t as part of thread suspension / exception handling.
  //
  // When a user thread is suspended or generates an exception (synthetic or architectural) that
  // might be observed by another process, we save user register state to the thread's arch_thread_t
  // so that it may be accessed by a debugger.  Upon leaving a suspended or exception state, we
  // restore user register state.
  //
  // See also |IsUserStateSavedLocked()| and |ScopedThreadExceptionContext|.
  bool user_state_saved_;

  // Provides a way to execute a custom logic when a thread must be migrated between CPUs.
  MigrateFn migrate_fn_;

  // Used to track threads that have set |migrate_fn_|. This is used to migrate threads before a CPU
  // is taken offline.
  fbl::DoublyLinkedListNodeState<Thread*> migrate_list_node_ TA_GUARDED(thread_lock);

  struct MigrateListTrait {
    static fbl::DoublyLinkedListNodeState<Thread*>& node_state(Thread& thread) {
      return thread.migrate_list_node_;
    }
  };
  using MigrateList = fbl::DoublyLinkedListCustomTraits<Thread*, MigrateListTrait>;

  // The global list of threads with migrate functions.
  static MigrateList migrate_list_ TA_GUARDED(thread_lock);
};

// For the moment, the arch-specific current thread implementations need to come here, after the
// Thread definition. One of the arches needs to know the structure of Thread to compute the offset
// that the hardware pointer holds into Thread.
#include <arch/current_thread.h>
Thread* Thread::Current::Get() { return arch_get_current_thread(); }

// TODO(johngro): Remove this when we have addressed fxbug.dev/33473.  Right now, this
// is used in only one place (x86_bringup_aps in arch/x86/smp.cpp) outside of
// thread.cpp.
//
// Normal users should only ever need to call either Thread::Create, or
// Thread::CreateEtc.
void init_thread_struct(Thread* t, const char* name);

// Other thread-system bringup functions.
void thread_init_early();
void thread_secondary_cpu_entry() __NO_RETURN;
void thread_construct_first(Thread* t, const char* name);

// Call the arch-specific signal handler.
extern "C" void arch_iframe_process_pending_signals(iframe_t* iframe);

// find a thread based on the thread id
// NOTE: used only for debugging, its a slow linear search through the
// global thread list
Thread* thread_id_to_thread_slow(zx_koid_t tid) TA_EXCL(thread_lock);

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
      Thread::Current::preemption_state().ReschedReenable();
    }
  }

  void Disable() {
    if (!started_) {
      Thread::Current::preemption_state().ReschedDisable();
      started_ = true;
    }
  }

  DISALLOW_COPY_ASSIGN_AND_MOVE(AutoReschedDisable);

 private:
  bool started_ = false;
};

// RAII helper that installs/removes an exception context and saves/restores user register state.
// The class operates on the current thread.
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
  explicit ScopedThreadExceptionContext(const arch_exception_context_t* context);
  ~ScopedThreadExceptionContext();
  DISALLOW_COPY_ASSIGN_AND_MOVE(ScopedThreadExceptionContext);

 private:
  Thread* thread_;
  const arch_exception_context_t* context_;
  bool need_to_remove_;
  bool need_to_restore_;
};

// RAII helper to enforce that a block of code does not allocate memory.
//
// See |Thread::Current::memory_allocation_state()|.
class ScopedMemoryAllocationDisabled {
 public:
  ScopedMemoryAllocationDisabled() { Thread::Current::memory_allocation_state().Disable(); }
  ~ScopedMemoryAllocationDisabled() { Thread::Current::memory_allocation_state().Enable(); }
  DISALLOW_COPY_ASSIGN_AND_MOVE(ScopedMemoryAllocationDisabled);
};

// These come last, after the definitions of both Thread and WaitQueue.
template <typename Callable>
void WaitQueueCollection::ForeachThread(const Callable& visit_thread) TA_REQ(thread_lock) {
  auto consider_queue = [&visit_thread](Thread* queue_head) TA_REQ(thread_lock) -> bool {
    // So, this is a bit tricky.  We need to visit each node in a
    // wait_queue priority level in a way which permits our visit_thread
    // function to remove the thread that we are visiting.
    //
    // Each priority level starts with a queue head which has a list of
    // more threads which exist at that priority level, but the queue
    // head itself is not a member of this list, so some special care
    // must be taken.
    //
    // Start with the queue_head and look up the next thread (if any) at
    // the priority level.  Visit the thread, and if (after visiting the
    // thread), the next thread has become the new queue_head, update
    // queue_head and keep going.
    //
    // If we advance past the queue head, but still have threads to
    // consider, switch to a more standard enumeration of the queue
    // attached to the queue_head.  We know at this point in time that
    // the queue_head can no longer change out from under us.
    //
    DEBUG_ASSERT(queue_head != nullptr);
    Thread* next;

    while (true) {
      next = nullptr;
      if (!queue_head->wait_queue_state().sublist_.is_empty()) {
        next = &queue_head->wait_queue_state().sublist_.front();
      }

      if (!visit_thread(queue_head)) {
        return false;
      }

      // Have we run out of things to visit?
      if (!next) {
        return true;
      }

      // If next is not the new queue head, stop.
      if (!next->wait_queue_state().IsHead()) {
        break;
      }

      // Next is the new queue head.  Update and keep going.
      queue_head = next;
    }

    // If we made it this far, then we must still have a valid next.
    DEBUG_ASSERT(next);
    do {
      Thread* t = next;
      auto iter = queue_head->wait_queue_state().sublist_.make_iterator(*t);
      ++iter;
      if (iter == queue_head->wait_queue_state().sublist_.end()) {
        next = nullptr;
      } else {
        next = &*iter;
      }

      if (!visit_thread(t)) {
        return false;
      }
    } while (next != nullptr);

    return true;
  };

  Thread* last_queue_head = nullptr;

  for (Thread& queue_head : heads_) {
    if ((last_queue_head != nullptr) && !consider_queue(last_queue_head)) {
      return;
    }
    last_queue_head = &queue_head;
  }

  if (last_queue_head != nullptr) {
    consider_queue(last_queue_head);
  }
}

inline WaitQueueHeadsTrait::NodeState& WaitQueueHeadsTrait::node_state(Thread& thread) {
  return thread.wait_queue_state().heads_node_;
}

inline WaitQueueSublistTrait::NodeState& WaitQueueSublistTrait::node_state(Thread& thread) {
  return thread.wait_queue_state().sublist_node_;
}

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_THREAD_H_
