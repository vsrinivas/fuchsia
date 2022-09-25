// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_THREAD_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_THREAD_H_

#include <debug.h>
#include <lib/backtrace.h>
#include <lib/fit/function.h>
#include <lib/relaxed_atomic.h>
#include <lib/zircon-internal/thread_annotations.h>
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
#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>
#include <fbl/wavl_tree_best_node_observer.h>
#include <kernel/cpu.h>
#include <kernel/deadline.h>
#include <kernel/koid.h>
#include <kernel/restricted_state.h>
#include <kernel/scheduler_state.h>
#include <kernel/spinlock.h>
#include <kernel/task_runtime_stats.h>
#include <kernel/thread_lock.h>
#include <kernel/timer.h>
#include <ktl/array.h>
#include <ktl/atomic.h>
#include <ktl/string_view.h>
#include <lockdep/thread_lock_state.h>
#include <vm/kstack.h>

class Dpc;
class OwnedWaitQueue;
class PreemptionState;
class StackOwnedLoanedPagesInterval;
class ThreadDispatcher;
class VmAspace;
class WaitQueue;
struct Thread;

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

// The PreemptDisabledToken (and its global singleton instance,
// |preempt_disabled_token|) are clang static analysis tokens which can be used
// to annotate methods as requiring that local preemption be disabled in order
// to operate properly.  See the AnnotatedAutoPreemptDisabler helper in
// kernel/auto_preempt_disabler.h for more details.
struct TA_CAP("token") PreemptDisabledToken {
 public:
  void AssertHeld() TA_ASSERT();

  PreemptDisabledToken() = default;
  PreemptDisabledToken(const PreemptDisabledToken&) = delete;
  PreemptDisabledToken(PreemptDisabledToken&&) = delete;
  PreemptDisabledToken& operator=(const PreemptDisabledToken&) = delete;
  PreemptDisabledToken& operator=(PreemptDisabledToken&&) = delete;

 private:
  friend class PreemptionState;
  void Acquire() TA_ACQ() {}
  void Release() TA_REL() {}
};

extern PreemptDisabledToken preempt_disabled_token;

// Whether a block or a sleep can be interrupted.
enum class Interruptible : bool { No, Yes };

// When signaling to a wait queue that the priority of one of its blocked
// threads has changed, this enum is used as a signal indicating whether or not
// the priority change should be propagated down the PI chain (if any) or not.
enum class PropagatePI : bool { No = false, Yes };

// A WaitQueueCollection is the data structure which holds a collection of
// threads which are currently blocked in a wait queue.  The data structure
// imposes a total ordering on the threads meant to represent the order in which
// the threads should be woken, from "most important" to "least important".
//
// One unusual property of the ordering implemented by a WaitQueueCollection is
// that, unlike an ordering determined by completely properties such as thread
// priority or weight, it is dynamic with respect to time.  This is to say that
// while at any instant in time there is always a specific order to the threads,
// as time advances, this order can change.  The ordering itself is determined
// by the nature of the various dynamic scheduling disciplines implemented by
// the Zircon scheduler.
//
// At any specific time |now|, the order of the collection is considered to
// be:
//
// 1) The deadline threads in the collection whose absolute deadlines are in the
//    future, sorted by ascending absolute deadline.  These are the threads who
//    still have a chance of meeting their absolute deadline, with the nearest
//    absolute deadline considered to be the most important.
// 2) The deadline threads in the collection whose absolute deadlines are in the
//    past, sorted by ascending relative deadline.  These are the threads who
//    have been blocked until after their last cycle's absolute deadline.  If
//    all threads were to be woken |now|, the thread with the minimum relative
//    deadline would be the thread which has the new absolute deadline across
//    the set.
// 3) The fair threads in the collection, sorted by their "virtual finish time".
//    This is equal to the start time of the thread plus the scheduler's maximum
//    target latency divided by the thread's weight (normalized to the range
//    (0.0, 1.0].  This is the same ordering imposed by the Scheduler's RunQueue
//    for fair threads, and is intended to prioritize higher weight threads,
//    while still ensuring some level of fairness over time.  The start time
//    represents the last time that a thread entered a run queue, and while high
//    weight threads will be chosen before low weight threads who arrived at
//    similar times, threads who arrived earlier (and have been waiting for
//    longer) will eventually end up being chosen, no matter how much weight
//    other threads in the collection have compared to it.
//    TODO(johngro): Instead of using the start time for the last time a
//    thread entered a RunQueue, should we use the time at which the thread
//    joined the wait queue instead?
//
// In an attempt to make the selection of the "best" thread in a wait queue as
// efficient as we can, in light of the dynamic nature of the total ordering, we
// use an "augmented" WAVL tree as our data structure, much like the scheduler's
// RunQueue.  The tree keeps all of its threads sorted according to a primary
// key representing the minimum absolute deadline or a modified version its
// virtual finish time, depending on the thread's scheduling discipline).
//
// The virtual finish time of threads is modified so that the MSB of the time is
// always set. This guarantees that fair threads _always_ come after in the
// sorting of threads.  Note that we could have also achieved this partitioning
// by tracking fair threads separately from deadline thread in a separate tree
// instance.  We keep things in a single tree (for now) in order to help to
// minimize the size of WaitQueueCollections to help control the size of objects
// in the kernel (such as the Mutex object).
//
// There should be no serious issue with using the MSB of the sort key in this
// fashion.  Absolute timestamps in zircon use signed 64 bit integers, and the
// monotonic clock is set at startup to start from zero, meaning that there is
// no real world case where we would be searching for a deadline thread to
// wake using a timestamp with the MSB set.
//
// Finally, we also maintain an addition augmented invariant such that: For
// every node (X) in the tree, the pointer to the thread with the minimum
// relative deadline in the subtree headed by X is maintained as nodes are
// inserted and removed.
//
// With these invariants in place, finding the best thread to run can be
// computed as follows.
//
// 1) If the left-most member of the tree has the MSB of its sorting key set,
//    then the thread is a fair thread, and there are _no_ deadline threads in
//    the tree.  Additionally, this thread has the minimum virtual finish time
//    across all of the fair threads in the tree, and therefore is the "best"
//    thread to unblock.  When the tree is in this state, selection is O(1).
// 2) Otherwise, there are deadline threads in the tree.  The tree is searched
//    to find the first thread whose absolute deadline is in the future,
//    relative to |now|.  If such a thread exists, then it is the "best" thread
//    to run right now and it is selected.  When the tree is in this state,
//    selection is O(log).
// 3) If there are no threads whose deadlines are in the future, the pointer to
//    the thread with the minimum relative deadline in the tree is chosen,
//    simply by fetching the best-in-subtree pointer maintained in |root()|.
//    While this operation is O(1), when the tree is this state, the over all
//    achieved order was O(log) because of the search which needed to happen
//    during step 2.
//
// Insert and remove order for the tree should be:
// 1) Insertions into the tree are always O(log).
// 2) Unlike a typical WAVL tree, removals of a specific thread from the tree
//    are O(log) instead of being amortized constant.  This is because of the
//    cost of restoring the augmented invariant after removal, which involves
//    walking from the point of removal up to the root of the tree.
//
// Finally:
// Please note that it is possible for the dynamic ordering defined above choose
// a deadline thread which is not currently eligible to run as the choice for
// "best thread".  This is because the scheduler does not currently demand that
// the absolute deadline of a thread be equal to when its period ends and its
// timeslice is eligible for refresh.
//
// While it is possible to account for this behavior as well, doing so is not
// without cost (both in WaitQueue object size and code complexity). This
// behavior is no different from the previous priority-based-ordering's
// behavior, where ineligible deadline threads could also be chosen.  The
// ability to specify a period different from a relative deadline is currently
// rarely used in the system, and we are moving in a direction of removing it
// entirely.  If the concept needs to be re-introduced at a later date, this
// data structure could be adjusted later on to order threads in phase 2 based
// on the earliest absolute deadline the could possible have based on earliest
// time that their period could be refreshed, and their relative deadline
// parameter.
class WaitQueueCollection {
 private:
  // fwd decls
  struct BlockedThreadTreeTraits;
  struct MinRelativeDeadlineTraits;

 public:
  using Key = ktl::pair<uint64_t, uintptr_t>;

  // Encapsulation of all the per-thread state for the WaitQueueCollection data structure.
  class ThreadState {
   public:
    ThreadState() = default;

    ~ThreadState();

    // Disallow copying.
    ThreadState(const ThreadState&) = delete;
    ThreadState& operator=(const ThreadState&) = delete;

    bool InWaitQueue() const { return blocked_threads_tree_node_.InContainer(); }

    zx_status_t BlockedStatus() const TA_REQ(thread_lock) { return blocked_status_; }

    void Block(Interruptible interruptible, zx_status_t status) TA_REQ(thread_lock);

    void UnblockIfInterruptible(Thread* thread, zx_status_t status)
        TA_REQ(thread_lock, preempt_disabled_token);

    void Unsleep(Thread* thread, zx_status_t status) TA_REQ(thread_lock);
    void UnsleepIfInterruptible(Thread* thread, zx_status_t status) TA_REQ(thread_lock);

    void UpdatePriorityIfBlocked(Thread* thread, int priority, PropagatePI propagate)
        TA_REQ(thread_lock, preempt_disabled_token);

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
    friend struct WaitQueueCollection::BlockedThreadTreeTraits;
    friend struct WaitQueueCollection::MinRelativeDeadlineTraits;

    // If blocked, a pointer to the WaitQueue the Thread is on.
    WaitQueue* blocking_wait_queue_ TA_GUARDED(thread_lock) = nullptr;

    // A list of the WaitQueues currently owned by this Thread.
    fbl::DoublyLinkedList<OwnedWaitQueue*> owned_wait_queues_ TA_GUARDED(thread_lock);

    // Node state for existing in WaitQueueCollection::threads_
    fbl::WAVLTreeNodeState<Thread*> blocked_threads_tree_node_;

    // Primary key used for determining our position in the collection of
    // blocked threads. Pre-computed during insert in order to save a time
    // during insert, rebalance, and search operations.
    uint64_t blocked_threads_tree_sort_key_{0};

    // State variable holding the pointer to the thread in our subtree with the
    // minimum relative deadline (if any).
    Thread* subtree_min_rel_deadline_thread_{nullptr};

    // Return code if woken up abnormally from suspend, sleep, or block.
    zx_status_t blocked_status_ = ZX_OK;

    // Dumping routines are allowed to see inside us.
    friend void dump_thread_locked(Thread* t, bool full_dump);

    // Are we allowed to be interrupted on the current thing we're blocked/sleeping on?
    Interruptible interruptible_ = Interruptible::No;
  };

  constexpr WaitQueueCollection() {}

  // The number of threads currently in the collection.
  uint32_t Count() const TA_REQ(thread_lock) { return static_cast<uint32_t>(threads_.size()); }

  // Peek at the first Thread in the collection.
  Thread* Peek(zx_time_t now) TA_REQ(thread_lock);
  const Thread* Peek(zx_time_t now) const TA_REQ(thread_lock) {
    return const_cast<WaitQueueCollection*>(this)->Peek(now);
  }

  // Add the Thread into its sorted location in the collection.
  void Insert(Thread* thread) TA_REQ(thread_lock);

  // Remove the Thread from the collection.
  void Remove(Thread* thread) TA_REQ(thread_lock);

  // Disallow copying.
  WaitQueueCollection(const WaitQueueCollection&) = delete;
  WaitQueueCollection& operator=(const WaitQueueCollection&) = delete;

 private:
  friend class WaitQueue;  // TODO(johngro): remove this when WaitQueue::BlockedPriority goes away.
  static constexpr uint64_t kFairThreadSortKeyBit = uint64_t{1} << 63;

  struct BlockedThreadTreeTraits {
    static Key GetKey(const Thread& thread);
    static bool LessThan(Key a, Key b) { return a < b; }
    static bool EqualTo(Key a, Key b) { return a == b; }
    static fbl::WAVLTreeNodeState<Thread*>& node_state(Thread& thread);
  };

  struct MinRelativeDeadlineTraits {
    // WAVLTreeBestNodeObserver template API
    using ValueType = Thread*;
    static ValueType GetValue(const Thread& node);
    static ValueType GetSubtreeBest(const Thread& node);
    static bool Compare(ValueType a, ValueType b);
    static void AssignBest(Thread& node, ValueType val);
    static void ResetBest(Thread& target);
  };

  using BlockedThreadTree = fbl::WAVLTree<Key, Thread*, BlockedThreadTreeTraits,
                                          fbl::DefaultObjectTag, BlockedThreadTreeTraits,
                                          fbl::WAVLTreeBestNodeObserver<MinRelativeDeadlineTraits>>;

  BlockedThreadTree threads_;
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
  static zx_status_t UnblockThread(Thread* t, zx_status_t wait_queue_error)
      TA_REQ(thread_lock, preempt_disabled_token);

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
  Thread* Peek(zx_time_t now) TA_REQ(thread_lock) { return collection_.Peek(now); }
  const Thread* Peek(zx_time_t now) const TA_REQ(thread_lock) { return collection_.Peek(now); }

  // Release one or more threads from the wait queue.
  // wait_queue_error = what WaitQueue::Block() should return for the blocking thread.
  //
  // Returns true if a thread was woken, and false otherwise.
  bool WakeOne(zx_status_t wait_queue_error) TA_REQ(thread_lock);

  void WakeAll(zx_status_t wait_queue_error) TA_REQ(thread_lock);

  // Whether the wait queue is currently empty.
  bool IsEmpty() const TA_REQ(thread_lock);

  uint32_t Count() const TA_REQ(thread_lock) { return collection_.Count(); }

  // Returns the highest priority of all the blocked threads on this WaitQueue.
  // Returns -1 if no threads are blocked.
  int BlockedPriority() const TA_REQ(thread_lock);

  // Used by WaitQueue and OwnedWaitQueue to manage changes to the maximum
  // priority of a wait queue due to external effects (thread priority change,
  // thread timeout, thread killed).
  void UpdatePriority(int old_prio) TA_REQ(thread_lock);

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
  void PriorityChanged(Thread* t, int old_prio, PropagatePI propagate)
      TA_REQ(thread_lock, preempt_disabled_token);

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
void dump_thread_tid(zx_koid_t tid, bool full) TA_EXCL(thread_lock);
void dump_thread_tid_locked(zx_koid_t tid, bool full) TA_REQ(thread_lock);

static inline void dump_thread_during_panic(Thread* t, bool full) TA_NO_THREAD_SAFETY_ANALYSIS {
  // Skip grabbing the lock if we are panic'ing
  dump_thread_locked(t, full);
}

static inline void dump_all_threads_during_panic(bool full) TA_NO_THREAD_SAFETY_ANALYSIS {
  // Skip grabbing the lock if we are panic'ing
  dump_all_threads_locked(full);
}

static inline void dump_thread_tid_during_panic(zx_koid_t tid,
                                                bool full) TA_NO_THREAD_SAFETY_ANALYSIS {
  // Skip grabbing the lock if we are panic'ing
  dump_thread_tid_locked(tid, full);
}

class PreemptionState {
 public:
  // Counters contained in state_ are limited to 15 bits.
  static constexpr uint32_t kMaxCountValue = 0x7fff;
  // The preempt disable count is in the lowest 15 bits.
  static constexpr uint32_t kPreemptDisableMask = kMaxCountValue;
  // The eager resched disable count is in the next highest 15 bits.
  static constexpr uint32_t kEagerReschedDisableShift = 15;
  static constexpr uint32_t kEagerReschedDisableMask = kMaxCountValue << kEagerReschedDisableShift;
  // Finally, the timeslice extension flags are the highest 2 bits.
  static constexpr uint32_t kTimesliceExtensionFlagsShift = 30;
  static constexpr uint32_t kTimesliceExtensionFlagsMask =
      ~(kPreemptDisableMask | kEagerReschedDisableMask);
  enum TimesliceExtensionFlags {
    // Thread has a timeslice extension that may or may not be Active.
    Present = 0b01 << kTimesliceExtensionFlagsShift,
    // Thread has an Active (in use) timeslice extension.
    Active = 0b10 << kTimesliceExtensionFlagsShift,
  };

  cpu_mask_t preempts_pending() const { return preempts_pending_.load(); }
  void preempts_pending_clear() { preempts_pending_.store(0); }
  void preempts_pending_add(cpu_mask_t mask) { preempts_pending_.fetch_or(mask); }

  bool PreemptIsEnabled() const {
    // Preemption is enabled iff both counts are zero and there's no runtime
    // extension.
    return state_.load() == 0;
  }

  uint32_t PreemptDisableCount() const { return PreemptDisableCount(state_.load()); }
  uint32_t EagerReschedDisableCount() const { return EagerReschedDisableCount(state_.load()); }

  // PreemptDisable() increments the preempt disable counter for the current
  // thread. While preempt disable is non-zero, preemption of the thread is
  // disabled, including preemption from interrupt handlers. During this time,
  // any call to Reschedule() will only record that a reschedule is pending, and
  // won't do a context switch.
  //
  // Note that this does not disallow blocking operations (e.g.
  // mutex.Acquire()). Disabling preemption does not prevent switching away from
  // the current thread if it blocks.
  //
  // A call to PreemptDisable() must be matched by a later call to
  // PreemptReenable() to decrement the preempt disable counter.
  void PreemptDisable() {
    const uint32_t old_state = state_.fetch_add(1);
    ASSERT(PreemptDisableCount(old_state) < kMaxCountValue);
  }

  // PreemptReenable() decrements the preempt disable counter and flushes any
  // pending local preemption operation.  Callers must ensure that they are
  // calling from a context where blocking is allowed, as the call may result in
  // the immediate preemption of the calling thread.
  void PreemptReenable() {
    const uint32_t old_state = state_.fetch_sub(1);
    ASSERT(PreemptDisableCount(old_state) > 0);

    // First, check for the expected situation of dropping the preempt count to zero
    // with a zero eager resched disable count and no timeslice extension.
    if (old_state == 1) {
      FlushPending(Flush::FlushLocal);
      return;
    }

    // Things must be more complicated.  Check for the various situations in
    // decreasing order of likeliness.

    // Are either of the counters non-zero?
    if (EagerReschedDisableCount(old_state) > 0 || PreemptDisableCount(old_state) > 1) {
      // We've got a non-zero count in one of the counters.
      return;
    }

    // The counters are both zero.  At this point, we must have a timeslice
    // extension installed.  This extension may be inactive, active and
    // not-yet-expired, or active and expired.

    // Is there an active extension?
    if (HasActiveTimesliceExtension(old_state)) {
      // Has it expired?
      if (ClearActiveTimesliceExtensionIfExpired()) {
        // It has.  We can flush.
        DEBUG_ASSERT(PreemptIsEnabled());
        FlushPending(Flush::FlushLocal);
        return;
      }
    }

    // We have an extension that's either inactive or active+unexpired.
  }

  void PreemptDisableAnnotated() TA_ACQ(preempt_disabled_token) {
    preempt_disabled_token.Acquire();
    PreemptDisable();
  }

  void PreemptReenableAnnotated() TA_REL(preempt_disabled_token) {
    preempt_disabled_token.Release();
    PreemptReenable();
  }

  // PreemptReenableDelayFlush() decrements the preempt disable counter, but
  // deliberately does _not_ flush any pending local preemption operation.
  // Instead, if local preemption has become enabled again after the count
  // drops, and the local pending bit is set, the method will clear the bit and
  // return true.  Otherwise, it will return false.
  //
  // This method may only be called when interrupts are disabled and blocking is
  // not allowed.
  //
  // Callers of this method are "taking" ownership of the responsibility to
  // ensure that preemption on the local CPU takes place in the near future
  // after the call if the method returns true.
  //
  // Use of this method is strongly discouraged outside of top-level interrupt
  // glue and early threading setup.
  //
  // TODO(johngro): Consider replacing the bool return type with a move-only
  // RAII type which wraps the bool, and ensures that preemption event _must_
  // happen, either by having the user call a method on the object to manually
  // force the preemption event, or when the object destructs.
  [[nodiscard]] bool PreemptReenableDelayFlush() {
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(arch_blocking_disallowed());

    const uint32_t old_state = state_.fetch_sub(1);
    ASSERT(PreemptDisableCount(old_state) > 0);

    // First, check for the expected situation of dropping the preempt count to zero
    // with a zero eager resched disable count and no timeslice extension.
    if (old_state == 1) {
      const cpu_mask_t local_mask = cpu_num_to_mask(arch_curr_cpu_num());
      const cpu_mask_t prev_mask = preempts_pending_.fetch_and(~local_mask);
      return (local_mask & prev_mask) != 0;
    }

    if (EagerReschedDisableCount(old_state) > 0 || PreemptDisableCount(old_state) > 1) {
      // We've got a non-zero count in one of the counters.
      return false;
    }

    // The counters are both zero.  At this point, we must have a timeslice
    // extension installed.  This extension may be inactive, active and
    // not-yet-expired, or active and expired.

    // Is there an active extension?
    if (HasActiveTimesliceExtension(old_state)) {
      // Has it expired?
      if (ClearActiveTimesliceExtensionIfExpired()) {
        // It has.
        DEBUG_ASSERT(PreemptIsEnabled());
        const cpu_mask_t local_mask = cpu_num_to_mask(arch_curr_cpu_num());
        const cpu_mask_t prev_mask = preempts_pending_.fetch_and(~local_mask);
        return (local_mask & prev_mask) != 0;
      }
    }

    // We have an extension that's either inactive or active+unexpired.
    return false;
  }

  // EagerReschedDisable() increments the eager resched disable counter for the
  // current thread. When early resched disable is non-zero, issuing local and
  // remote preemptions is disabled, including from interrupt handlers. During
  // this time, any call to Reschedule() or other scheduler entry points that
  // imply a reschedule will only record the pending reschedule for the affected
  // CPU, but will not perform reschedule IPIs or a local context switch.
  //
  // As with PreemptDisable, blocking operations are still allowed while
  // eager resched disable is non-zero.
  //
  // A call to EagerReschedDisable() must be matched by a later call to
  // EagerReschedReenable() to decrement the eager resched disable counter.
  void EagerReschedDisable() {
    const uint32_t old_state = state_.fetch_add(1 << kEagerReschedDisableShift);
    ASSERT(EagerReschedDisableCount(old_state) < kMaxCountValue);
  }

  // EagerReschedReenable() decrements the eager resched disable counter and
  // flushes pending local and/or remote preemptions if enabled, respectively.
  void EagerReschedReenable() {
    const uint32_t old_state = state_.fetch_sub(1 << kEagerReschedDisableShift);
    ASSERT(EagerReschedDisableCount(old_state) > 0);

    // First check the expected case.
    if (old_state == 1 << kEagerReschedDisableShift) {
      // Counts are both zero and there's no timeslice extension.
      //
      // Flushing all might reschedule this CPU, make sure it's OK to block.
      FlushPending(Flush::FlushAll);
      return;
    }

    if (EagerReschedDisableCount(old_state) > 1) {
      // Nothing to do since eager resched disable implies preempt disable.
      return;
    }

    // We know we can at least flush remote.  Can we also flush local?
    if (PreemptDisableCount(old_state) > 0) {
      // Nope, we've got a non-zero preempt disable count.
      FlushPending(Flush::FlushRemote);
      return;
    }

    // Is there an active extension?
    if (HasActiveTimesliceExtension(old_state)) {
      // Has it expired?
      if (ClearActiveTimesliceExtensionIfExpired()) {
        // Yes, preempt disable count is zero and the active extension has
        // expired.  We can flush all.
        DEBUG_ASSERT(PreemptIsEnabled());
        FlushPending(Flush::FlushAll);
        return;
      }
      // Extension is active, can't flush local.
    }

    // We have an inactive extension or an unexpired active extension.  Either
    // way, we can flush remote, but not local.
    FlushPending(Flush::FlushRemote);
  }

  void EagerReschedDisableAnnotated() TA_ACQ(preempt_disabled_token) {
    preempt_disabled_token.Acquire();
    EagerReschedDisable();
  }

  void EagerReschedReenableAnnotated() TA_REL(preempt_disabled_token) {
    preempt_disabled_token.Release();
    EagerReschedReenable();
  }

  // Sets a timeslice extension if one is not already set.
  //
  // This method should only be called in normal thread context.
  //
  // Returns false if a timeslice extension was already present or if the
  // supplied duration is <= 0.
  //
  // Note: It OK to call this from a context where preemption is (hard)
  // disabled.  If preemption is requested while the preempt disable count is
  // non-zero and a timeslice extension is in place, the extension will be
  // activated, but preemption will not occur until the count has dropped to
  // zero and the extension has expired or has been clear.
  bool SetTimesliceExtension(zx_duration_t extension_duration) {
    if (extension_duration <= 0) {
      return false;
    }

    uint32_t state = state_.load();
    if (HasTimesliceExtension(state)) {
      return false;
    }
    timeslice_extension_.store(extension_duration);
    // Make sure that the timeslice extension value becomes visible to an
    // interrupt handler in this thread prior to the state_ flag becoming
    // visible.  See comment at |timeslice_extension_|.
    ktl::atomic_signal_fence(ktl::memory_order_release);
    state_.fetch_or(TimesliceExtensionFlags::Present);
    return true;
  }

  // Unconditionally clears any timeslice extension.
  //
  // This method must be called in normal thread context because it may trigger
  // local preemption.
  void ClearTimesliceExtension() {
    // Clear any present timeslice extension.
    const uint32_t old_state = state_.fetch_and(~kTimesliceExtensionFlagsMask);
    // Are the counters both zero?
    if ((old_state & ~kTimesliceExtensionFlagsMask) == 0) {
      FlushPending(Flush::FlushLocal);
    }
  }

  // PreemptSetPending() marks a pending preemption for the given CPUs.
  //
  // This is similar to Reschedule(), except that it may only be used inside an
  // interrupt handler while interrupts and preemption are disabled, between
  // PreemptDisable() and PreemptReenable(). It is similar to Reschedule(),
  // except that it does not need to be called with thread_lock held.
  void PreemptSetPending(cpu_mask_t reschedule_mask = cpu_num_to_mask(arch_curr_cpu_num())) {
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(arch_blocking_disallowed());
    DEBUG_ASSERT(!PreemptIsEnabled());

    preempts_pending_.fetch_or(reschedule_mask);

    // Are we pending for the local CPU?
    if (reschedule_mask & cpu_num_to_mask(arch_curr_cpu_num() == 0)) {
      // Nope.
      return;
    }

    EvaluateTimesliceExtension();
  }

  // Evaluate the thread's timeslice extension (if present), activating or
  // expiring it as necessary.
  //
  // Returns whether preemption is enabled.
  bool EvaluateTimesliceExtension() {
    const uint32_t old_state = state_.load();
    if (old_state == 0) {
      // No counts, no extension.  The common case.
      return true;
    }

    if (!HasTimesliceExtension(old_state)) {
      // No extension, but we have a non-zero count.
      return false;
    }

    if (HasActiveTimesliceExtension(old_state)) {
      if (!ClearActiveTimesliceExtensionIfExpired()) {
        return false;
      }
      // The active extension has expired.  If the counts are both zero, then
      // we're ready for preemption.
      return (old_state & ~kTimesliceExtensionFlagsMask) == 0;
    }

    // We have a not-yet-active extension.  Time to activate it.
    //
    // See comment at |timeslice_extension_| for why the signal fence is needed.
    ktl::atomic_signal_fence(ktl::memory_order_acquire);
    const zx_duration_t extension_duration = timeslice_extension_.load();
    if (extension_duration <= 0) {
      // Already expired.
      state_.fetch_and(~kTimesliceExtensionFlagsMask);
      return (old_state & ~kTimesliceExtensionFlagsMask) == 0;
    }
    const zx_time_t deadline = zx_time_add_duration(current_time(), extension_duration);
    timeslice_extension_deadline_.store(deadline);
    // See comment at |timeslice_extension_deadline_| for why the signal fence
    // is needed.
    ktl::atomic_signal_fence(ktl::memory_order_release);
    state_.fetch_or(TimesliceExtensionFlags::Active);
    SetPreemptionTimerForExtension(deadline);
    return false;
  }

 private:
  friend class PreemptDisableTestAccess;

  static inline uint32_t EagerReschedDisableCount(uint32_t state) {
    return (state & kEagerReschedDisableMask) >> kEagerReschedDisableShift;
  }

  static inline uint32_t PreemptDisableCount(uint32_t state) { return state & kPreemptDisableMask; }

  static inline bool HasTimesliceExtension(uint32_t state) {
    return (state & TimesliceExtensionFlags::Present) != 0;
  }

  static inline bool HasActiveTimesliceExtension(uint32_t state) {
    return (state & TimesliceExtensionFlags::Active) != 0;
  }

  // A non-inlined helper method to set the preemption timer when a timeslice
  // has been extended.  This must be non-inline to avoid an #include cycle with
  // percpu.h and thread.h.
  static void SetPreemptionTimerForExtension(zx_time_t deadline);

  // Checks whether the active timeslice extension has expired and if so, clears
  // it and returns true.
  //
  // Should only be called when there is an active timeslice extension.
  bool ClearActiveTimesliceExtensionIfExpired() {
    // Has the extension expired?
    //
    // See comment at |timeslice_extension_deadline_| for why the signal fence is needed.
    ktl::atomic_signal_fence(ktl::memory_order_acquire);
    if (current_time() >= timeslice_extension_deadline_.load()) {
      state_.fetch_and(~kTimesliceExtensionFlagsMask);
      return true;
    }
    return false;
  }

  enum Flush { FlushLocal = 0x1, FlushRemote = 0x2, FlushAll = FlushLocal | FlushRemote };

  // Flushes local, remote, or all pending preemptions.
  //
  // This method is split in two so that the early out case of no pending
  // preemptions may be inlined without creating a header include cycle.
  void FlushPending(Flush flush) {
    // Early out to avoid unnecessarily taking the thread lock. This check races
    // any potential flush due to context switch, however, the context switch can
    // only clear bits that would have been flushed below, no new pending
    // preemptions are possible in the mask bits indicated by |flush|.
    if (likely(preempts_pending_.load() == 0)) {
      return;
    }
    FlushPendingContinued(flush);
  }
  void FlushPendingContinued(Flush flush);

  // state_ contains three fields:
  //
  //  * a 15-bit preempt disable counter (bits 0-14)
  //  * a 15-bit eager resched disable counter (bits 15-29)
  //  * a 2-bit for TimesliceExtensionFlags (bits 30-31)
  //
  // This is a single field so that both counters and the flags can be compared
  // against zero with a single memory access and comparison.
  //
  // state_'s counts are modified by interrupt handlers, but the counts are
  // always restored to their original value before the interrupt handler
  // returns, so modifications are not visible to the interrupted thread.
  RelaxedAtomic<uint32_t> state_{};

  // preempts_pending_ tracks pending reschedules to both local and remote CPUs
  // due to activity in the context of the current thread.
  //
  // This value can be changed asynchronously by an interrupt handler.
  //
  // preempts_pending_ should only be non-zero:
  //  * if PreemptDisableCount() or EagerReschedDisable() are non-zero, or
  //  * after PreemptDisableCount() or EagerReschedDisable() have been
  //    decremented, while preempts_pending_ is being checked.
  RelaxedAtomic<cpu_mask_t> preempts_pending_{};

  // The maximum duration of the thread's timeslice extension.
  //
  // This field is only valid when |state_|'s
  // |kTimeSliceExtensionFlags::Present| flag it set.
  //
  // This field may only be accessed by its owning thread or in an interrupt
  // context of the owning thread.  When reading this field, be sure to issue an
  // atomic_signal_fence (compiler barrier) with acquire semantics after
  // observing the Present flag.  Likewise, when writing this field, use an
  // atomic_signal_fence with release semantics prior to setting the Present
  // flag.  By using these fences, we ensure the flag and field value remain in
  // sync.
  RelaxedAtomic<zx_duration_t> timeslice_extension_{};

  // The deadline at which the thread timeslice extension expires.
  //
  // This field is only valid when |kTimeSliceExtensionFlags::Active| flag it
  // set.
  //
  // This field may only be accessed by its owning thread or in an interrupt
  // context of the owning thread.  When reading this field, be sure to issue an
  // atomic_signal_fence (compiler barrier) with acquire semantics after
  // observing the Active flag.  Likewise, when writing this field, use an
  // atomic_signal_fence with release semantics prior to setting the Active
  // flag.  By using these fences, we ensure the flag and field value remain in
  // sync.
  RelaxedAtomic<zx_time_t> timeslice_extension_deadline_{};
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
    disable_count_ = disable_count_ + 1;
    ktl::atomic_signal_fence(ktl::memory_order_seq_cst);
  }

  void Enable() {
    ktl::atomic_signal_fence(ktl::memory_order_seq_cst);
    DEBUG_ASSERT(disable_count_ > 0);
    disable_count_ = disable_count_ - 1;
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

  // Returns the lock that protects the thread's internal state, particularly with respect to
  // scheduling.
  //
  // TODO(eieio): Returns the thread lock for now, but will be replaced by a member variable when
  // the thread lock is removed.
  MonitoredSpinLock& get_lock() TA_RET_CAP(thread_lock) { return thread_lock; }

  // Get the associated ThreadDispatcher.
  ThreadDispatcher* user_thread() { return user_thread_.get(); }
  const ThreadDispatcher* user_thread() const { return user_thread_.get(); }

  // Returns the koid of the associated ProcessDispatcher for user threads or
  // ZX_KOID_INVLID for kernel threads.
  zx_koid_t pid() const { return pid_; }

  // Returns the koid of the associated ThreadDispatcher for user threads or an
  // independent koid for kernel threads.
  zx_koid_t tid() const { return tid_; }

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

  // Checks whether the kill or suspend signal has been raised. If kill has been
  // raised, then `ZX_ERR_INTERNAL_INTR_KILLED` will be returned. If suspend has
  // been raised, then `ZX_ERR_INTERNAL_INTR_RETRY` will be returned. Otherwise,
  // `ZX_OK` will be returned.
  zx_status_t CheckKillOrSuspendSignal() const;

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
  using MigrateFn = fit::inline_function<void(Thread* thread, MigrateStage stage), sizeof(void*)>
      TA_REQ(thread_lock);

  void SetMigrateFn(MigrateFn migrate_fn) TA_EXCL(thread_lock);
  void SetMigrateFnLocked(MigrateFn migrate_fn) TA_REQ(thread_lock);
  void CallMigrateFnLocked(MigrateStage stage) TA_REQ(thread_lock);

  // Call |migrate_fn| for each thread that was last run on the current CPU.
  static void CallMigrateFnForCpuLocked(cpu_num_t cpu) TA_REQ(thread_lock);

  void OwnerName(char (&out_name)[ZX_MAX_NAME_LEN]);
  // Return the number of nanoseconds a thread has been running for.
  zx_duration_t Runtime() const;

  // Last cpu this thread was running on, or INVALID_CPU if it has never run.
  cpu_num_t LastCpu() const TA_EXCL(thread_lock);
  cpu_num_t LastCpuLocked() const;

  // Return true if thread has been signaled.
  bool IsSignaled() { return signals() != 0; }
  bool IsIdle() const { return !!(flags_ & THREAD_FLAG_IDLE); }

  // Returns true if this Thread's user state has been saved.
  //
  // Caller must hold the thread lock.
  bool IsUserStateSavedLocked() const TA_REQ(thread_lock) {
    thread_lock.AssertHeld();
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
    static void Kill();
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

    // |policy_exception_code| should be a ZX_EXCP_POLICY_CODE_* value.
    static void SignalPolicyException(uint32_t policy_exception_code,
                                      uint32_t policy_exception_data);

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

    static RestrictedState& restricted_state() { return Thread::Current::Get()->restricted_state_; }

    // Generate a backtrace for the calling thread.
    //
    // |out_bt| will be reset() prior to be filled in and if a backtrace cannot
    // be obtained, it will be left empty.
    static void GetBacktrace(Backtrace& out_bt);

    // Generate a backtrace for the calling thread starting at frame pointer |fp|.
    //
    // |out_bt| will be reset() prior to be filled in and if a backtrace cannot
    // be obtained, it will be left empty.
    static void GetBacktrace(vaddr_t fp, Backtrace& out_bt);

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
      dump_thread_tid_during_panic(tid, full);
    }
  };

  // Trait for the global Thread list.
  struct ThreadListTrait {
    static fbl::DoublyLinkedListNodeState<Thread*>& node_state(Thread& thread) {
      return thread.thread_list_node_;
    }
  };
  using List = fbl::DoublyLinkedListCustomTraits<Thread*, ThreadListTrait>;

  // Traits for the temporary unblock list, used to batch-unblock threads.
  //
  // TODO(johngro): look into options for optimizing this.  It should be
  // possible to share node storage with that used for wait queues (since a
  // thread needs to have been removed from a wait queue before being sent to
  // Scheduler::Unblock).
  struct UnblockListTrait {
    static fbl::DoublyLinkedListNodeState<Thread*>& node_state(Thread& thread) {
      return thread.unblock_list_node_;
    }
  };
  using UnblockList = fbl::DoublyLinkedListCustomTraits<Thread*, UnblockListTrait>;

  // Stats for a thread's runtime.
  class RuntimeStats {
   public:
    struct SchedulerStats {
      thread_state state = thread_state::THREAD_INITIAL;  // last state
      zx_time_t state_time = 0;                           // when the thread entered state
      zx_duration_t cpu_time = 0;                         // time spent on CPU
      zx_duration_t queue_time = 0;                       // time spent ready to start running
    };

    const SchedulerStats& GetSchedulerStats() const { return sched_; }

    // Update scheduler stats with newer content.
    //
    // Adds to CPU and queue time, but sets the given state directly.
    void UpdateSchedulerStats(const SchedulerStats& other) {
      sched_.cpu_time = zx_duration_add_duration(sched_.cpu_time, other.cpu_time);
      sched_.queue_time = zx_duration_add_duration(sched_.queue_time, other.queue_time);
      sched_.state = other.state;
      sched_.state_time = other.state_time;
    }

    // Add time spent handling page faults.
    // Safe for concurrent use.
    void AddPageFaultTicks(zx_ticks_t ticks) {
      // Ignore overflow: it will take hundreds of years to overflow, and even if it
      // does overflow, this is primarily used to compute relative (rather than absolute)
      // values, which still works after overflow.
      page_fault_ticks_.fetch_add(ticks);
    }

    // Add time spent contented on locks.
    // Safe for concurrent use.
    void AddLockContentionTicks(zx_ticks_t ticks) {
      // Ignore overflow: it will take hundreds of years to overflow, and even if it
      // does overflow, this is primarily used to compute relative (rather than absolute)
      // values, which still works after overflow.
      lock_contention_ticks_.fetch_add(ticks);
    }

    // Get the current TaskRuntimeStats, including the current scheduler state.
    TaskRuntimeStats TotalRuntime() const {
      TaskRuntimeStats ret = {
          .cpu_time = sched_.cpu_time,
          .queue_time = sched_.queue_time,
          .page_fault_ticks = page_fault_ticks_.load(),
          .lock_contention_ticks = lock_contention_ticks_.load(),
      };
      if (sched_.state == thread_state::THREAD_RUNNING) {
        ret.cpu_time = zx_duration_add_duration(
            ret.cpu_time, zx_duration_sub_duration(current_time(), sched_.state_time));
      } else if (sched_.state == thread_state::THREAD_READY) {
        ret.queue_time = zx_duration_add_duration(
            ret.queue_time, zx_duration_sub_duration(current_time(), sched_.state_time));
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

   private:
    SchedulerStats sched_;
    RelaxedAtomic<zx_ticks_t> page_fault_ticks_{0};
    RelaxedAtomic<zx_ticks_t> lock_contention_ticks_{0};
  };

  struct Linebuffer {
    size_t pos = 0;
    ktl::array<char, 128> buffer{};
  };

  void UpdateSchedulerStats(const RuntimeStats::SchedulerStats& stats) TA_REQ(thread_lock);

  void DumpDuringPanic(bool full) TA_NO_THREAD_SAFETY_ANALYSIS {
    dump_thread_during_panic(this, full);
  }

  // Accessors into Thread state. When the conversion to all-private
  // members is complete (bug 54383), we can revisit the overall
  // Thread API.

  thread_state state() const { return scheduler_state_.state(); }

  // The scheduler can set threads to be running, or to be ready to run.
  void set_running() { scheduler_state_.set_state(THREAD_RUNNING); }
  void set_ready() { scheduler_state_.set_state(THREAD_READY); }
  // While wait queues can set threads to be blocked.
  void set_blocked() { scheduler_state_.set_state(THREAD_BLOCKED); }
  void set_blocked_read_lock() { scheduler_state_.set_state(THREAD_BLOCKED_READ_LOCK); }
  // The thread can set itself to be sleeping.
  void set_sleeping() { scheduler_state_.set_state(THREAD_SLEEPING); }
  void set_death() { scheduler_state_.set_state(THREAD_DEATH); }
  void set_suspended() { scheduler_state_.set_state(THREAD_SUSPENDED); }

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

  unsigned int signals() const { return signals_.load(ktl::memory_order_relaxed); }

  bool has_migrate_fn() const { return migrate_fn_ != nullptr; }
  bool migrate_pending() const { return migrate_pending_; }

  TaskState& task_state() { return task_state_; }
  const TaskState& task_state() const { return task_state_; }

  PreemptionState& preemption_state() { return preemption_state_; }
  const PreemptionState& preemption_state() const { return preemption_state_; }

  SchedulerState& scheduler_state() { return scheduler_state_; }
  const SchedulerState& scheduler_state() const { return scheduler_state_; }

  WaitQueueCollection::ThreadState& wait_queue_state() { return wait_queue_state_; }
  const WaitQueueCollection::ThreadState& wait_queue_state() const { return wait_queue_state_; }

#if WITH_LOCK_DEP
  lockdep::ThreadLockState& lock_state() { return lock_state_; }
  const lockdep::ThreadLockState& lock_state() const { return lock_state_; }
#endif

  RestrictedState& restricted_state() { return restricted_state_; }
  const RestrictedState& restricted_state() const { return restricted_state_; }

  arch_thread& arch() { return arch_; }
  const arch_thread& arch() const { return arch_; }

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

  // Generate a backtrace for |this| thread.
  //
  // |this| must be blocked, sleeping or suspended (i.e. not running).
  //
  // |out_bt| will be reset() prior to be filled in and if a backtrace cannot be
  // obtained, it will be left empty.
  void GetBacktrace(Backtrace& out_bt) TA_EXCL(thread_lock);

  StackOwnedLoanedPagesInterval* stack_owned_loaned_pages_interval() {
    return stack_owned_loaned_pages_interval_;
  }

  // Returns the last flow id allocated by TakeNextLockFlowId() for this thread.
  uint64_t lock_flow_id() const {
#if LOCK_TRACING_ENABLED
    return lock_flow_id_;
#else
    return 0;
#endif
  }

  // Returns a unique flow id for lock contention tracing. The same value is
  // returned by lock_flow_id() until another id is allocated for this thread
  // by calling this method again.
  uint64_t TakeNextLockFlowId() {
#if LOCK_TRACING_ENABLED
    return lock_flow_id_ = lock_flow_id_generator_ += 1;
#else
    return 0;
#endif
  }

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

  // StackOwnedLoanedPagesInterval is the only public way to set/clear the
  // stack_owned_loaned_pages_interval().
  friend class StackOwnedLoanedPagesInterval;

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
  struct MigrateListTrait {
    static fbl::DoublyLinkedListNodeState<Thread*>& node_state(Thread& thread) {
      return thread.migrate_list_node_;
    }
  };
  using MigrateList = fbl::DoublyLinkedListCustomTraits<Thread*, MigrateListTrait>;

  // The global list of threads with migrate functions.
  static MigrateList migrate_list_ TA_GUARDED(thread_lock);

  Canary canary_;

  // These fields are among the most active in the thread. They are grouped
  // together near the front to improve cache locality.
  unsigned int flags_{};
  ktl::atomic<unsigned int> signals_{};
  SchedulerState scheduler_state_;
  WaitQueueCollection::ThreadState wait_queue_state_;
  TaskState task_state_;
  PreemptionState preemption_state_;
  MemoryAllocationState memory_allocation_state_;
  RestrictedState restricted_state_;
  // This is part of ensuring that all stack ownership of loaned pages can be boosted in priority
  // via priority inheritance if a higher priority thread is trying to reclaim the loaned pages.
  StackOwnedLoanedPagesInterval* stack_owned_loaned_pages_interval_ = nullptr;

#if WITH_LOCK_DEP
  // state for runtime lock validation when in thread context
  lockdep::ThreadLockState lock_state_;
#endif

  // pointer to the kernel address space this thread is associated with
  VmAspace* aspace_{};

  // Saved by SignalPolicyException() to store the type of policy error, and
  // passed to exception disptach in ProcessPendingSignals().
  uint32_t extra_policy_exception_code_ TA_GUARDED(thread_lock) = 0;
  uint32_t extra_policy_exception_data_ TA_GUARDED(thread_lock) = 0;

  // Strong reference to user thread if one exists for this thread.
  // In the common case freeing Thread will also free ThreadDispatcher when this
  // reference is dropped.
  fbl::RefPtr<ThreadDispatcher> user_thread_;

  // When user_thread_ is set, these values are copied from ThreadDispatcher and
  // its parent ProcessDispatcher. Kernel threads maintain an independent tid.
  zx_koid_t tid_ = KernelObjectId::Generate();
  zx_koid_t pid_ = ZX_KOID_INVALID;

  // Architecture-specific stuff.
  arch_thread arch_{};

  KernelStack stack_;

  // This is used by dispatcher.cc:SafeDeleter.
  void* recursive_object_deletion_list_ = nullptr;

  // This always includes the trailing NUL.
  char name_[ZX_MAX_NAME_LEN]{};

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
  bool user_state_saved_{};

#if LOCK_TRACING_ENABLED
  // The flow id allocated before blocking on the last lock.
  RelaxedAtomic<uint64_t> lock_flow_id_{0};

  // Generates unique flow ids for tracing lock contention.
  inline static RelaxedAtomic<uint64_t> lock_flow_id_generator_{0};
#endif

  // For threads with migration functions, indicates whether a migration is in progress. When true,
  // the migrate function has been called with Before but not yet with After.
  bool migrate_pending_{};

  // Provides a way to execute a custom logic when a thread must be migrated between CPUs.
  MigrateFn migrate_fn_;

  // Used to track threads that have set |migrate_fn_|. This is used to migrate
  // threads before a CPU is taken offline.
  fbl::DoublyLinkedListNodeState<Thread*> migrate_list_node_ TA_GUARDED(thread_lock);

  // Node storage for existing on the global thread list.
  fbl::DoublyLinkedListNodeState<Thread*> thread_list_node_ TA_GUARDED(thread_lock);

  // Node storage for existing on the temporary batch unblock list.
  fbl::DoublyLinkedListNodeState<Thread*> unblock_list_node_ TA_GUARDED(thread_lock);
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
void construct_thread(Thread* t, const char* name);

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

// WaitQueue collection trait implementations.  While typically these would be
// implemented in-band in the trait class itself, these definitions must come
// last, after the definition Thread.  This is because the traits (defined in
// WaitQueueCollection) need to understand the layout of Thread in order to be
// able to access both scheduler state and wait queue state variables.
inline WaitQueueCollection::Key WaitQueueCollection::BlockedThreadTreeTraits::GetKey(
    const Thread& thread) {
  // TODO(johngro): consider extending FBL to support a "MultiWAVLTree"
  // implementation which would allow for nodes with identical keys, breaking
  // ties (under the hood) using pointer value.  This way, we would not need to
  // manifest our own pointer in GetKey or in our key type.
  return {thread.wait_queue_state().blocked_threads_tree_sort_key_,
          reinterpret_cast<uintptr_t>(&thread)};
}

inline fbl::WAVLTreeNodeState<Thread*>& WaitQueueCollection::BlockedThreadTreeTraits::node_state(
    Thread& thread) {
  return thread.wait_queue_state().blocked_threads_tree_node_;
}

inline Thread* WaitQueueCollection::MinRelativeDeadlineTraits::GetValue(const Thread& thread) {
  // TODO(johngro), consider pre-computing this value so it is just a fetch
  // instead of a branch.
  return (thread.scheduler_state().discipline() == SchedDiscipline::Fair)
             ? nullptr
             : const_cast<Thread*>(&thread);
}

inline Thread* WaitQueueCollection::MinRelativeDeadlineTraits::GetSubtreeBest(
    const Thread& thread) {
  return thread.wait_queue_state().subtree_min_rel_deadline_thread_;
}

inline bool WaitQueueCollection::MinRelativeDeadlineTraits::Compare(Thread* a, Thread* b) {
  // The thread pointer value of a non-deadline thread is null, an non-deadline
  // threads are always the worst choice when choosing the thread with the
  // minimum relative deadline.
  // clang-format off
  if (a == nullptr) { return false; }
  if (b == nullptr) { return true; }
  const SchedDuration a_deadline = a->scheduler_state().deadline().deadline_ns;
  const SchedDuration b_deadline = b->scheduler_state().deadline().deadline_ns;
  return (a_deadline < b_deadline) || ((a_deadline == b_deadline) && (a < b));
  // clang-format on
}

inline void WaitQueueCollection::MinRelativeDeadlineTraits::AssignBest(Thread& thread,
                                                                       Thread* val) {
  thread.wait_queue_state().subtree_min_rel_deadline_thread_ = val;
}

inline void WaitQueueCollection::MinRelativeDeadlineTraits::ResetBest(Thread& thread) {
  // In a debug build, zero out the subtree best as we leave the collection.
  // This can help to find bugs by allowing us to assert that the value is zero
  // during insertion, however it is not strictly needed in a production build
  // and can be skipped.
#ifdef DEBUG_ASSERT_IMPLEMENTED
  thread.wait_queue_state().subtree_min_rel_deadline_thread_ = nullptr;
#endif
}

inline void PreemptDisabledToken::AssertHeld() {
  DEBUG_ASSERT(Thread::Current::preemption_state().PreemptIsEnabled() == false);
}

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_THREAD_H_
