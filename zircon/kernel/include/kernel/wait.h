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

__BEGIN_CDECLS

// wait queue stuff
#define WAIT_QUEUE_MAGIC (0x77616974)  // 'wait'

typedef struct wait_queue {
  // Note: Wait queues come in 2 flavors (traditional and owned) which are
  // distinguished using the magic number.  The point here is that, unlike
  // most other magic numbers in the system, the wait_queue_t serves a
  // functional purpose beyond checking for corruption debug builds.
  int magic;
  int count;
  struct list_node heads;
} wait_queue_t;

#define WAIT_QUEUE_INITIAL_VALUE_MAGIC(q, _magic) \
  { .magic = (_magic), .count = 0, .heads = LIST_INITIAL_VALUE((q).heads), }

#define WAIT_QUEUE_INITIAL_VALUE(q) WAIT_QUEUE_INITIAL_VALUE_MAGIC(q, WAIT_QUEUE_MAGIC)

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

// wait queue primitive
// NOTE: must be inside critical section when using these
void wait_queue_init(wait_queue_t* wait);

void wait_queue_destroy(wait_queue_t*);

// block on a wait queue.
// return status is whatever the caller of wait_queue_wake_*() specifies.
// a deadline other than ZX_TIME_INFINITE will abort at the specified time
// and return ZX_ERR_TIMED_OUT. a deadline in the past will immediately return.

zx_status_t wait_queue_block(wait_queue_t*, zx_time_t deadline) TA_REQ(thread_lock);

// block on a wait queue, ignoring existing signals in |signal_mask|.
// return status is whatever the caller of wait_queue_wake_*() specifies or
// ZX_ERR_TIMED_OUT if the deadline has elapsed or is in the past.
// will never timeout when called with a deadline of ZX_TIME_INFINITE.
zx_status_t wait_queue_block_etc(wait_queue_t*, const Deadline& deadline, uint signal_mask,
                                 ResourceOwnership reason) TA_REQ(thread_lock);

// returns the highest priority of all the blocked threads on this wait queue.
// returns -1 if no threads are blocked.

int wait_queue_blocked_priority(const wait_queue_t*) TA_REQ(thread_lock);

// returns the current highest priority blocked thread on this wait queue, or
// null if no threads are blocked.
Thread* wait_queue_peek(wait_queue_t*) TA_REQ(thread_lock);

// release one or more threads from the wait queue.
// reschedule = should the system reschedule if any is released.
// wait_queue_error = what wait_queue_block() should return for the blocking thread.

int wait_queue_wake_one(wait_queue_t*, bool reschedule, zx_status_t wait_queue_error)
    TA_REQ(thread_lock);

int wait_queue_wake_all(wait_queue_t*, bool reschedule, zx_status_t wait_queue_error)
    TA_REQ(thread_lock);

// Dequeue the first waiting thread, and set its blocking status, then return a
// pointer to the thread which was dequeued.  Do not actually schedule the
// thread to run.
Thread* wait_queue_dequeue_one(wait_queue_t* wait, zx_status_t wait_queue_error)
    TA_REQ(thread_lock);

// Dequeue the specified thread and set its blocked_status.  Do not actually
// schedule the thread to run.
void wait_queue_dequeue_thread(wait_queue_t* wait, Thread* t, zx_status_t wait_queue_error)
    TA_REQ(thread_lock);

// Move the specified thread from the source wait queue to the dest wait queue.
void wait_queue_move_thread(wait_queue_t* source, wait_queue_t* dest, Thread* t)
    TA_REQ(thread_lock);

// is the wait queue currently empty
bool wait_queue_is_empty(const wait_queue_t*) TA_REQ(thread_lock);

// remove a specific thread out of a wait queue it's blocked on
zx_status_t wait_queue_unblock_thread(Thread* t, zx_status_t wait_queue_error) TA_REQ(thread_lock);

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
bool wait_queue_priority_changed(Thread* t, int old_prio, PropagatePI propagate)
    TA_REQ(thread_lock);

// validate that the queue of a given wait queue is valid
void wait_queue_validate_queue(wait_queue_t* wait) TA_REQ(thread_lock);

__END_CDECLS

#ifdef __cplusplus

class WaitQueue : protected wait_queue_t {
 public:
  constexpr WaitQueue() : WaitQueue(WAIT_QUEUE_MAGIC) {}
  ~WaitQueue() { wait_queue_destroy(this); }

  WaitQueue(WaitQueue&) = delete;
  WaitQueue(WaitQueue&&) = delete;
  WaitQueue& operator=(WaitQueue&) = delete;
  WaitQueue& operator=(WaitQueue&&) = delete;

  static zx_status_t UnblockThread(Thread* t, zx_status_t wait_queue_error) TA_REQ(thread_lock) {
    return wait_queue_unblock_thread(t, wait_queue_error);
  }

  zx_status_t Block(const Deadline& deadline) TA_REQ(thread_lock) {
    return wait_queue_block_etc(this, deadline, 0, ResourceOwnership::Normal);
  }

  zx_status_t BlockReadLock(const Deadline& deadline) TA_REQ(thread_lock) {
    return wait_queue_block_etc(this, deadline, 0, ResourceOwnership::Reader);
  }

  Thread* Peek() TA_REQ(thread_lock) { return wait_queue_peek(this); }

  int WakeOne(bool reschedule, zx_status_t wait_queue_error) TA_REQ(thread_lock) {
    return wait_queue_wake_one(this, reschedule, wait_queue_error);
  }

  int WakeAll(bool reschedule, zx_status_t wait_queue_error) TA_REQ(thread_lock) {
    return wait_queue_wake_all(this, reschedule, wait_queue_error);
  }

  bool IsEmpty() const TA_REQ(thread_lock) { return (this->count == 0); }

  uint32_t Count() const TA_REQ(thread_lock) { return this->count; }

  Thread* DequeueOne(zx_status_t wait_queue_error) TA_REQ(thread_lock) {
    return wait_queue_dequeue_one(this, wait_queue_error);
  }

  void DequeueThread(Thread* t, zx_status_t wait_queue_error) TA_REQ(thread_lock) {
    wait_queue_dequeue_thread(this, t, wait_queue_error);
  }

 protected:
  explicit constexpr WaitQueue(int magic)
      : wait_queue_t(WAIT_QUEUE_INITIAL_VALUE_MAGIC(*this, magic)) {}

  static void MoveThread(WaitQueue* source, WaitQueue* dest, Thread* t) TA_REQ(thread_lock) {
    return wait_queue_move_thread(source, dest, t);
  }
};

#endif  // __cplusplus

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_WAIT_H_
