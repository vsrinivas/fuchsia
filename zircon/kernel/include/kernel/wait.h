// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_WAIT_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_WAIT_H_

#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include <arch/defines.h>
#include <arch/ops.h>
#include <arch/thread.h>
#include <kernel/deadline.h>
#include <kernel/spinlock.h>
#include <kernel/thread_lock.h>
#include <kernel/timer.h>

// When blocking this enum indicates the kind of resource ownership that is being waited for
// that is causing the block.
enum class ResourceOwnership {
  // Blocking is either not for any particular resource, or it is to wait for
  // exclusive access to a resource.
  Normal,
  // Blocking is happening whilst waiting for shared read access to a resource.
  Reader,
};

// When signaling to a wait queue that the priority of one of its blocked
// threads has changed, this enum is used as a signal indicating whether or not
// the priority change should be propagated down the PI chain (if any) or not.
enum class PropagatePI : bool { No = false, Yes };

// Encapsulation of all the per-thread state for the wait queue data structure.
struct WaitQueueState {
  WaitQueueState() = default;

  // Disallow copying.
  WaitQueueState(const WaitQueueState&) = delete;
  WaitQueueState& operator=(const WaitQueueState&) = delete;

  bool InWaitQueue() const { return list_in_list(&queue_node_); }

  struct list_node queue_node_;
  struct list_node wait_queue_heads_node_;
};

// Encapsulation of the data structure backing the wait queue.
//
// This maintains an ordered collection of Threads.
//
// All such collections are protected by the thread_lock.
struct WaitQueueCollection {
  constexpr WaitQueueCollection() {}

  // The number of threads currently in the collection.
  uint32_t Count() const TA_REQ(thread_lock) { return private_count_; }

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

  // These are morally private. Eventually, Thread will not be required to be POD, and we can make
  // it so.
  int private_count_ = 0;
  struct list_node private_heads_ = LIST_INITIAL_VALUE(private_heads_);
};

// NOTE: must be inside critical section when using these
class WaitQueue {
  // TODO(kulakowski) Currently, all of this is public, until Thread is completely migrated off
  // list_nodes, which force it to be standard layout.
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
  zx_status_t Block(const Deadline& deadline) TA_REQ(thread_lock) {
    return BlockEtc(deadline, 0, ResourceOwnership::Normal);
  }

  // Block on a wait queue with a zx_time_t-typed deadline.
  zx_status_t Block(zx_time_t deadline) TA_REQ(thread_lock) {
    return BlockEtc(Deadline::no_slack(deadline), 0, ResourceOwnership::Normal);
  }

  // Block on a wait queue, ignoring existing signals in |signal_mask|.
  // The returned status is whatever the caller of WaitQueue::Wake_*() specifies, or
  // ZX_ERR_TIMED_OUT if the deadline has elapsed or is in the past.
  // This will never timeout when called with a deadline of Deadline::infinite().
  zx_status_t BlockEtc(const Deadline& deadline, uint signal_mask, ResourceOwnership reason)
      TA_REQ(thread_lock);

  zx_status_t BlockReadLock(const Deadline& deadline) TA_REQ(thread_lock) {
    return BlockEtc(deadline, 0, ResourceOwnership::Reader);
  }

  // Returns the current highest priority blocked thread on this wait queue, or
  // nullptr if no threads are blocked.
  Thread* Peek() TA_REQ(thread_lock);
  const Thread* Peek() const TA_REQ(thread_lock);

  // Release one or more threads from the wait queue.
  // reschedule = should the system reschedule if any is released.
  // wait_queue_error = what WaitQueue::Block() should return for the blocking thread.
  int WakeOne(bool reschedule, zx_status_t wait_queue_error) TA_REQ(thread_lock);

  int WakeAll(bool reschedule, zx_status_t wait_queue_error) TA_REQ(thread_lock);

  // Whether the wait queue is currently empty.
  bool IsEmpty() const TA_REQ(thread_lock);

  uint32_t Count() const TA_REQ(thread_lock) { return collection_.Count(); }

  // Dequeue the first waiting thread, and set its blocking status, then return a
  // pointer to the thread which was dequeued.  Do not actually schedule the
  // thread to run.
  Thread* DequeueOne(zx_status_t wait_queue_error) TA_REQ(thread_lock);

  // Dequeue the specified thread and set its blocked_status.  Do not actually
  // schedule the thread to run.
  void DequeueThread(Thread* t, zx_status_t wait_queue_error) TA_REQ(thread_lock);

  explicit constexpr WaitQueue(uint32_t magic) : magic_(magic) {}

  // Move the specified thread from the source wait queue to the dest wait queue.
  static void MoveThread(WaitQueue* source, WaitQueue* dest, Thread* t) TA_REQ(thread_lock);

  // Returns the highest priority of all the blocked threads on this WaitQueue.
  // Returns -1 if no threads are blocked.
  int BlockedPriority() const TA_REQ(thread_lock);

  // A thread's priority has changed.  Update the wait queue bookkeeping to
  // properly reflect this change.
  //
  // If |propagate| is PropagatePI::Yes, call into the wait queue code to
  // propagate the priority change down the PI chain (if any).  Then returns true
  // if the change of priority has affected the priority of another thread due to
  // priority inheritance, or false otherwise.
  //
  // If |propagate| is PropagatePI::No, do not attempt to propagate the PI change.
  // This is the mode used by OwnedWaitQueue during a batch update of a PI chain.
  static bool PriorityChanged(Thread* t, int old_prio, PropagatePI propagate) TA_REQ(thread_lock);

  // Validate that the queue of a given WaitQueue is valid.
  void ValidateQueue() TA_REQ(thread_lock);

  // Note: Wait queues come in 2 flavors (traditional and owned) which are
  // distinguished using the magic number.  The point here is that, unlike
  // most other magic numbers in the system, the wait_queue_t serves a
  // functional purpose beyond checking for corruption debug builds.
  static constexpr uint32_t kMagic = fbl::magic("wait");
  uint32_t magic_;

  WaitQueueCollection collection_;
};

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_WAIT_H_
