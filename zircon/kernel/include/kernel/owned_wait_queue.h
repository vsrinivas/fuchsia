// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_OWNED_WAIT_QUEUE_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_OWNED_WAIT_QUEUE_H_

#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>
#include <kernel/thread.h>
#include <kernel/thread_lock.h>
#include <kernel/wait.h>

namespace internal {

// fwd decl
// we don't want to drag all of wait_queue_internal.h into this header file, but
// we need to be friends with this internal function, so we just fwd decl it
// here instead..
bool wait_queue_waiters_priority_changed(wait_queue_t* wq, int old_prio) TA_REQ(thread_lock);

}  // namespace internal

// Owned wait queues are an extension of wait queues which adds the concept of
// ownership for use when priority inheritance semantics are needed.
//
// An owned wait queue maintains an unmanaged pointer to a Thread in order to
// track who owns it at any point in time.  In addition, it contains node state
// which can be used by the owning thread in order to track the wait queues that
// the thread is currently an owner of.  This also makes use of unmanaged
// pointer.
//
// It should be an error for any thread to destruct while it owns an
// OwnedWaitQueue.  Likewise, it should be an error for any wait queue to
// destruct while it has an owner.  These invariants are enforced in the
// destructor for OwnedWaitQueue and Thread.  This enforcement is considered
// to be the reasoning why holding unmanaged pointers is considered to be safe.
//
class OwnedWaitQueue : public WaitQueue, public fbl::DoublyLinkedListable<OwnedWaitQueue*> {
 public:
  // A small helper class which can be injected into Wake and Requeue
  // operations to allow calling code to get a callback for each thread which
  // is either woken, or requeued.  This callback serves two purposes...
  //
  // 1) It allows the caller to perform some limited filtering operations, and
  //    to choose which thread (if any) becomes the new owner of the queue.
  //    See the comments in the |Action| enum member for details.
  // 2) It gives code such as |FutexContext| a chance to perform their own
  //    per-thread bookkeeping as the wait queue code chooses which threads to
  //    either wake or re-queue.
  //
  // Note that during a wake or requeue operation, the threads being
  // considered will each be presented to the user provided Hook (if any)
  // by the OwnedWaitQueue code before deciding whether or not to actually
  // wake or requeue the thread.
  class Hook {
   public:
    // A set of 4 actions which may be taken when considering whether or not
    // to wake or requeue a thread.  If no user supplied Hook is provided
    // for a given operation, the default behavior will be to return
    // Action::SelectAndKeepGoing.
    enum class Action {
      // Do not wake or requeue this thread, do not declare it to be the
      // owner of anything.  Simply move on to the next thread (if
      // possible).
      Skip,

      // Do not wake or requeue this thread and stop considering threads.
      Stop,

      // Select this thread to be either woken or requeued, then continue
      // to consider more threads (if any).  Do not assign this thread to
      // be the owner.
      SelectAndKeepGoing,

      // Select this thread to be either woken or requeued, then stop
      // considering threads.  Do not assign this thread to be the owner.
      SelectAndStop,

      // Select this thread to be either woken or requeued, assign it to
      // to be the owner of the queue, then stop considering more threads.
      // It is illegal to wake a thread and assign it as the owner for the
      // queue if at least one thread has already been woken.
      SelectAndAssignOwner,
    };

    using Callback = Action (*)(Thread* thrd, void* ctx);

    Hook() : cbk_(nullptr) {}
    Hook(Callback cbk, void* ctx) : cbk_(cbk), ctx_(ctx) {}

    Action operator()(Thread* thrd) const TA_REQ(thread_lock) {
      return cbk_ ? cbk_(thrd, ctx_) : Action::SelectAndKeepGoing;
    }

   private:
    Callback cbk_;
    void* ctx_;
  };

  static constexpr int MAGIC = fbl::magic("ownq");
  constexpr OwnedWaitQueue() : WaitQueue(MAGIC) {}
  ~OwnedWaitQueue();

  // No copy or move is permitted.
  DISALLOW_COPY_ASSIGN_AND_MOVE(OwnedWaitQueue);

  // Release ownership of all wait queues currently owned by |t| and update
  // bookkeeping as appropriate.  This is meant to be called from the thread
  // itself and therefor it is assumed that the thread in question is not
  // blocked on any other wait queues.
  static void DisownAllQueues(Thread* t) TA_REQ(thread_lock);

  // const accessor for the owner member.
  Thread* owner() const TA_REQ(thread_lock) { return owner_; }

  // Debug Assert wrapper which skips the thread analysis checks just to
  // assert that a specific queue is unowned.  Used by FutexContext
  void AssertNotOwned() const TA_NO_THREAD_SAFETY_ANALYSIS { DEBUG_ASSERT(owner_ == nullptr); }

  // Assign ownership of this wait queue to |new_owner|, or explicitly release
  // ownership if |new_owner| is nullptr.
  //
  // Note, if the new owner exists, but is dead or dying, it will not be
  // permitted to become the new owner of the wait_queue.  Any existing owner
  // will be replaced with no owner in this situation.
  //
  // Returns true if a local reschedule is required, or false otherwise.
  bool AssignOwner(Thread* new_owner) TA_REQ(thread_lock) __WARN_UNUSED_RESULT {
    DEBUG_ASSERT(magic == MAGIC);

    // If the new owner is the same as the old owner, then we have nothing
    // special to do here.  Just short-circuit.
    if (new_owner == owner()) {
      return false;
    }

    return UpdateBookkeeping(new_owner, wait_queue_blocked_priority(this));
  }

  // Block the current thread on this wait queue, and re-assign ownership to
  // the specified thread (or remove ownership if new_owner is null);
  //
  // Note, if the new owner exists, but is dead or dying, it will not be
  // permitted to become the new owner of the wait_queue.  Any existing owner
  // will be replaced with no owner in this situation.
  zx_status_t BlockAndAssignOwner(const Deadline& deadline, Thread* new_owner,
                                  ResourceOwnership resource_ownership) TA_REQ(thread_lock);

  // Wake the up to specified number of threads from the wait queue and then
  // handle the ownership bookkeeping based on what the Hook told us to do.
  // See |Hook::Action| for details.
  //
  // Returns true if a local reschedule is required, or false otherwise.
  // Appropriate IPIs will already have been sent.
  bool WakeThreads(uint32_t wake_count, Hook on_thread_wake_hook = {})
      TA_REQ(thread_lock) __WARN_UNUSED_RESULT;

  // A specialization of WakeThreads which will...
  //
  // 1) Wake the number of threads indicated by |wake_count|
  // 2) Move the number of threads indicated by |requeue_count| to the |requeue_target|.
  // 3) Update ownership bookkeeping as indicated by |owner_action| and |requeue_owner|.
  //
  // This method is used by futexes in order to implement futex_requeue.  It
  // is wrapped up into a specialized form instead of broken into individual
  // parts in order to minimize any thrash in re-computing effective
  // priorities for PI purposes.  We don't want to re-evaluate ownership or PI
  // pressure until after all of the changes to wait queue have taken place.
  //
  // |requeue_target| *must* be non-null.  If there is no |requeue_target|,
  // use WakeThreads instead.
  //
  // Note, if the |requeue_owner| exists, but is dead or dying, it will not be
  // permitted to become the new owner of the |requeue_target|.  Any existing
  // owner will be replaced with no owner in this situation.
  //
  // Returns true if a local reschedule is required, or false otherwise.
  bool WakeAndRequeue(uint32_t wake_count, OwnedWaitQueue* requeue_target, uint32_t requeue_count,
                      Thread* requeue_owner, Hook on_thread_wake_hook = {},
                      Hook on_thread_requeue_hook = {}) TA_REQ(thread_lock) __WARN_UNUSED_RESULT;

 private:
  // Give permission to the wait_queue_t thunk to call the
  // WaitersPriorityChanged method (below).
  friend bool internal::wait_queue_waiters_priority_changed(wait_queue_t* wq, int old_prio);

  // A internal helper function which enumerates the wait_queue_t's
  // queue-of-queues structure in a fashion which allows us to remove the
  // threads in question as they are presented to our injected function for
  // consideration.
  //
  // Callable should be a lambda which takes a Thread* for consideration and
  // returns a bool.  If it returns true, iteration continues, otherwise it
  // immediately stops.
  template <typename Callable>
  void ForeachThread(const Callable& visit_thread) TA_REQ(thread_lock) {
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
        next = list_peek_head_type(&queue_head->queue_node_, Thread, queue_node_);

        if (!visit_thread(queue_head)) {
          return false;
        }

        // Have we run out of things to visit?
        if (!next) {
          return true;
        }

        // If next is not the new queue head, stop.
        if (!list_in_list(&next->wait_queue_heads_node_)) {
          break;
        }

        // Next is the new queue head.  Update and keep going.
        queue_head = next;
      }

      // If we made it this far, then we must still have a valid next.
      DEBUG_ASSERT(next);
      do {
        Thread* t = next;
        next = list_next_type(&queue_head->queue_node_, &t->queue_node_, Thread, queue_node_);

        if (!visit_thread(t)) {
          return false;
        }
      } while (next != nullptr);

      return true;
    };

    Thread* last_queue_head = nullptr;
    Thread* queue_head;

    list_for_every_entry (&this->heads, queue_head, Thread, wait_queue_heads_node_) {
      if ((last_queue_head != nullptr) && !consider_queue(last_queue_head)) {
        return;
      }
      last_queue_head = queue_head;
    }

    if (last_queue_head != nullptr) {
      consider_queue(last_queue_head);
    }
  }

  // Called whenever the pressure of a wait queue currently owned by |t| has
  // just changed.  Propagates priority inheritance side effects, but do not
  // send any IPIs.  Simply update the accum_cpu_mask to indicate which CPUs
  // were affected by the change.
  //
  // It is an error to call this function if |old_prio| == |new_prio|.  Be
  // sure to check inline before calling.
  //
  // Returns true if a local reschedule is required, or false otherwise.
  static bool QueuePressureChanged(Thread* t, int old_prio, int new_prio,
                                   cpu_mask_t* accum_cpu_mask) TA_REQ(thread_lock);

  // A hook called by the WaitQueue level when the maximum priority across all
  // current waiters has changed.
  //
  // Returns true if a local reschedule is required, or false otherwise.
  bool WaitersPriorityChanged(int old_prio) TA_REQ(thread_lock) __WARN_UNUSED_RESULT;

  // Updates ownership bookkeeping and deals with priority inheritance side
  // effects.  Called by internal code, typically after changes to the
  // contents of the queue have been made which may have an effect of the
  // maximum priority of the set of waiters.
  //
  // |new_owner|
  //   A pointer to the thread which should be the owner of this wait queue,
  //   or nullptr if this queue should have no owner.
  //
  // |old_prio|
  //   The priority of this wait queue as recorded by the caller before
  //   they started to make changes to the queue's contents.
  //
  // |accum_cpu_mask|
  //   An optional pointer to a cpu_mask_t.  When non-null, UpdateBookkeeping
  //   will accumulate into this mask the CPUs which have been affected by the
  //   PI side effects of updating this bookkeeping.  When nullptr,
  //   UpdateBookkeeping will automatically update kernel counters and send
  //   IPIs to processors which have been affected by the PI side effects.
  //
  // Returns true if a local reschedule is required, or false otherwise.
  bool UpdateBookkeeping(Thread* new_owner, int old_prio, cpu_mask_t* out_accum_cpu_mask = nullptr)
      TA_REQ(thread_lock) __WARN_UNUSED_RESULT;

  // Wake the specified number of threads from the wait queue, and return the
  // new owner (first thread woken) via the |out_new_owner| out param, or
  // nullptr if there should be no new owner.  This code is shared by Wake as
  // well as WakeAndRequeue.  Doing so allows us to preserve common code, and
  // to defer the PI pressure recalculations until the point at which all of
  // the queue manipulations have taken place.
  //
  // Returns true if a local reschedule is required, or false otherwise.
  bool WakeThreadsInternal(uint32_t wake_count, Thread** out_new_owner, Hook on_thread_wake_hook)
      TA_REQ(thread_lock) __WARN_UNUSED_RESULT;

  Thread* owner_ TA_GUARDED(thread_lock) = nullptr;
};

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_OWNED_WAIT_QUEUE_H_
