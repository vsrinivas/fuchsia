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
class OwnedWaitQueue : protected WaitQueue, public fbl::DoublyLinkedListable<OwnedWaitQueue*> {
 public:
  // We want to limit access to our base WaitQueue's methods, but not all of
  // them.  Make public the methods which should be safe for folks to use from
  // the OwnedWaitQueue level of things.
  //
  // This list is pretty short right now, and there are probably other methods
  // which could be added safely (WakeOne, WakeAll, Peek, etc...) we'd rather
  // keep the list as short as possible for now, and only expand it when there
  // is a tangible need, and a thorough review for safety.
  //
  // The general rule of thumb here is that code which knows that it using an
  // OwnedWaitQueue should go through the OWQ specific APIs instead of
  // attempting to use the base WaitQueue APIs.
  using WaitQueue::Count;
  using WaitQueue::IsEmpty;

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
    // A set of 3 actions which may be taken when considering whether or not
    // to wake or requeue a thread.  If no user supplied Hook is provided
    // for a given operation, the default behavior will be to return
    // Action::SelectAndKeepGoing.
    enum class Action {
      // Do not wake or requeue this thread and stop considering threads.
      Stop,

      // Select this thread to be either woken or requeued, then continue
      // to consider more threads (if any).  Do not assign this thread to
      // be the owner.
      SelectAndKeepGoing,

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

  static constexpr uint32_t kOwnedMagic = fbl::magic("ownq");
  constexpr OwnedWaitQueue() : WaitQueue(kOwnedMagic) {}
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
  void AssignOwner(Thread* new_owner) TA_REQ(thread_lock, preempt_disabled_token) {
    DEBUG_ASSERT(magic() == kOwnedMagic);
    if (new_owner != owner()) {
      UpdateBookkeeping(new_owner, BlockedPriority());
    }
  }

  // Block the current thread on this wait queue, and re-assign ownership to
  // the specified thread (or remove ownership if new_owner is null);
  //
  // Note, if the new owner exists, but is dead or dying, it will not be
  // permitted to become the new owner of the wait_queue.  Any existing owner
  // will be replaced with no owner in this situation.
  zx_status_t BlockAndAssignOwner(const Deadline& deadline, Thread* new_owner,
                                  ResourceOwnership resource_ownership, Interruptible interruptible)
      TA_REQ(thread_lock, preempt_disabled_token);

  // Wake the up to specified number of threads from the wait queue and then
  // handle the ownership bookkeeping based on what the Hook told us to do.
  // See |Hook::Action| for details.
  void WakeThreads(uint32_t wake_count, Hook on_thread_wake_hook = {})
      TA_REQ(thread_lock, preempt_disabled_token);

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
  void WakeAndRequeue(uint32_t wake_count, OwnedWaitQueue* requeue_target, uint32_t requeue_count,
                      Thread* requeue_owner, Hook on_thread_wake_hook = {},
                      Hook on_thread_requeue_hook = {}) TA_REQ(thread_lock, preempt_disabled_token);

 private:
  // Give permission to the WaitQueue thunk to call the
  // WaitersPriorityChanged method (below).
  friend void WaitQueue::UpdatePriority(int old_prio);

  // Called whenever the pressure of a wait queue currently owned by |t| has
  // just changed.  Propagates priority inheritance side effects.
  //
  // It is an error to call this function if |old_prio| == |new_prio|.  Be
  // sure to check inline before calling.
  static void QueuePressureChanged(Thread* t, int old_prio, int new_prio)
      TA_REQ(thread_lock, preempt_disabled_token);

  // A hook called by the WaitQueue level when the maximum priority across all
  // current waiters has changed.
  void WaitersPriorityChanged(int old_prio) TA_REQ(thread_lock, preempt_disabled_token);

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
  void UpdateBookkeeping(Thread* new_owner, int old_prio)
      TA_REQ(thread_lock, preempt_disabled_token);

  // Wake the specified number of threads from the wait queue, and return the
  // new owner (first thread woken) via the |out_new_owner| out param, or
  // nullptr if there should be no new owner.  This code is shared by Wake as
  // well as WakeAndRequeue.  Doing so allows us to preserve common code, and
  // to defer the PI pressure recalculations until the point at which all of
  // the queue manipulations have taken place.
  void WakeThreadsInternal(uint32_t wake_count, Thread** out_new_owner, zx_time_t now,
                           Hook on_thread_wake_hook) TA_REQ(thread_lock, preempt_disabled_token);

  Thread* owner_ TA_GUARDED(thread_lock) = nullptr;
};

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_OWNED_WAIT_QUEUE_H_
