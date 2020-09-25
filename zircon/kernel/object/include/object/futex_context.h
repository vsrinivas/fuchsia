// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_FUTEX_CONTEXT_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_FUTEX_CONTEXT_H_

#include <lib/user_copy/user_ptr.h>
#include <zircon/types.h>

#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/ref_ptr.h>
#include <kernel/lockdep.h>
#include <kernel/mutex.h>
#include <kernel/owned_wait_queue.h>
#include <kernel/thread_lock.h>
#include <ktl/move.h>
#include <ktl/unique_ptr.h>

class ThreadDispatcher;

// FutexContex
//
// A FutexContext is the object which manages the state of all of the active
// futexes for a user-mode process.  Each ProcessDispatcher in the system will
// have a single FutexContext contained within it, and the objects should exist
// no-where else in the system.
//
// FutexContexts manage a pool of FutexContext::FutexStates which are
// contributed by threads created within the process.  This pool guarantees that
// threads are guaranteed to be able to allocate a FutexState object in O(1)
// time whenever they perform a FutexWait operation, as a Futex is only "active"
// when it has any waiters.  See (Grow|Shrink)FutexStatePool comments as well as
// the FutexState's notes (below) for more details.
//
// The remaining methods in the public interface implement the 3 primary futex
// syscall operations (Wait, Wake, and Requeue) as well as the one
// test/diagnostic operation (GetOwner).  See the zircon syscall documentation
// for further details.
//
class FutexContext {
 public:
  // Owner action is an enum used to signal what to do when threads are woken
  // from a futex.  The defined behaviors are as follows.
  //
  // RELEASE
  // Remove any owner regardless of how many threads are woken (including zero
  // threads)
  //
  // ASSIGN_WOKEN
  // Only permitted when wake_count is exactly 1.  Assign ownership to the
  // thread who was woken if there was a thread to wake, and there are still
  // threads left in the futex after waking.  Otherwise, set the futex queue
  // owner to nothing.
  //
  enum class OwnerAction {
    RELEASE,
    ASSIGN_WOKEN,
  };

  FutexContext();
  ~FutexContext();

  // Called as ThreadDispatchers are created and destroyed in order to ensure
  // that there are always two FutexStates for each ThreadDispatcher in a
  // process.
  //
  // Why two and not one?  Because of the FutexRequeue operation.  Without
  // requeue, we would only need one, since a futex with waiters requires one
  // futex state, and there can be at most N futexes with waiters, where N is
  // the number of threads in a process.
  //
  // During FutexRequeue, however, a thread needs to grab a hold of two futex
  // contexts at the same time.  In addition, the thread performing this
  // operation is no longer holding a process wide futex context lock.  Instead,
  // it simply locks in order to activate the FutexStates and then unlocks.
  // Another thread can attempt a requeue in parallel, or it could exit in
  // parallel.  If only one FutexState were added for each thread, it would be
  // possible to run out of FutexStates if these operations were happening in
  // parallel.
  //
  zx_status_t GrowFutexStatePool();
  void ShrinkFutexStatePool();

  // FutexWait first verifies that the integer pointed to by |value_ptr| still equals
  // |current_value|. If the test fails, FutexWait returns BAD_STATE.  Otherwise it will block the
  // current thread until the |deadline| passes, or until the thread is woken by a FutexWake or
  // FutexRequeue operation on the same |value_ptr| futex.
  //
  // Note that this method and FutexRequeue both take a user mode handle instead of having the
  // syscall dispatch layer resolve the handle into a thread before proceeding.  This is because
  // we need to perform the current_value == *value_ptr check before attempting to validate the
  // thread handle, and this check needs to happen inside of the futex context lock.  To do
  // otherwise leaves the potential to hit a race condition where we end up appearing to violate
  // the "bad handle" policy when actually we didn't.  See fxbug.dev/34382 for details.
  zx_status_t FutexWait(user_in_ptr<const zx_futex_t> value_ptr, zx_futex_t current_value,
                        zx_handle_t new_futex_owner, const Deadline& deadline);

  // FutexWake will wake up to |wake_count| number of threads blocked on the |value_ptr| futex.
  //
  // If owner_action is set to RELEASE, then the futex's owner will be set to nullptr in the
  // process.  If the owner_action is set to ASSIGN_WOKEN, then the wake_count *must* be 1, and
  // the futex's owner will be set to the thread which was woken during the operation, or nullptr
  // if no thread was woken.
  zx_status_t FutexWake(user_in_ptr<const zx_futex_t> value_ptr, uint32_t wake_count,
                        OwnerAction owner_action);

  // FutexRequeue first verifies that the integer pointed to by |wake_ptr| still equals
  // |current_value|. If the test fails, FutexRequeue returns BAD_STATE.  Otherwise it will wake
  // up to |wake_count| number of threads blocked on the |wake_ptr| futex.  If any other threads
  // remain blocked on on the |wake_ptr| futex, up to |requeue_count| of them will then be
  // requeued to the tail of the list of threads blocked on the |requeue_ptr| futex.
  //
  // If owner_action is set to RELEASE, then the futex's owner will be set to nullptr in the
  // process.  If the owner_action is set to ASSIGN_WOKEN, then the wake_count *must* be 1, and
  // the futex's owner will be set to the thread which was woken during the operation, or nullptr
  // if no thread was woken.
  zx_status_t FutexRequeue(user_in_ptr<const zx_futex_t> wake_ptr, uint32_t wake_count,
                           zx_futex_t current_value, OwnerAction owner_action,
                           user_in_ptr<const zx_futex_t> requeue_ptr, uint32_t requeue_count,
                           zx_handle_t new_requeue_owner_handle);

  // Get the KOID of the current owner of the specified futex, if any, or ZX_KOID_INVALID if there
  // is no known owner.
  zx_status_t FutexGetOwner(user_in_ptr<const zx_futex_t> value_ptr, user_out_ptr<zx_koid_t> koid);

 private:
  template <typename GuardType>
  zx_status_t FutexWaitInternal(user_in_ptr<const zx_futex_t> value_ptr, zx_futex_t current_value,
                                ThreadDispatcher* futex_owner_thread, Thread* new_owner,
                                GuardType&& adopt_new_owner_guard, zx_status_t validator_status,
                                const Deadline& deadline);

  template <typename GuardType>
  zx_status_t FutexRequeueInternal(user_in_ptr<const zx_futex_t> wake_ptr, uint32_t wake_count,
                                   zx_futex_t current_value, OwnerAction owner_action,
                                   user_in_ptr<const zx_futex_t> requeue_ptr,
                                   uint32_t requeue_count, ThreadDispatcher* requeue_owner_thread,
                                   Thread* new_requeue_owner, GuardType&& adopt_new_owner_guard,
                                   zx_status_t validator_status);

  // Notes about FutexState lifecycle.
  // aka. Why is this safe?
  //
  // FutexState objects are used to track the state of any futex which currently
  // has waiters.  Currently, each thread in a process allocates two FutexStates
  // and contributes them to its process' futex context's free pool.  When the
  // thread exits, it take two FutexStates out of the free pool and lets them
  // expire.
  //
  // There is a master spin lock for each process which protects the sets of
  // active and free FutexStates.  Any time a thread needs to work with futex
  // ID X, it must first obtain the process-wide pool lock and either find the
  // FutexState in the active set with that ID, or activate one from the free
  // list.  After this, the process wide pool lock is immediately released.
  //
  // In order to keep this FutexState from disappearing out from under
  // the thread during its Wait/Wake/Requeue operation, a "pending operation"
  // ref count is increased in the FutexState object.  FutexStates are returned
  // to the free pool _only_ when the pending operation count reaches zero.
  //
  // FutexState objects are managed using ktl::unique_ptr.  At all times, a
  // FutexState will be in one of three states.
  //
  // 1) A member of a FutexContext's active_futexes_ hashtable.  Futexes in this state are
  //    currently involved in at least one futex operation.  Their futex ID will
  //    be non-zero as will their pending operation count..
  // 2) A member of a FutexContext's free_futexes_ list.  These futexes are
  //    not currently in use, but are available to be allocated and used.
  //    Their futex ID and pending operation count will be zero.
  // 3) A member of neither.  These futexes have been created, but not added
  //    to the pool yet, or removed from the free list by a thread which is
  //    exiting.  Their futex ID and pending operation count will be zero.
  //
  class FutexState : public fbl::DoublyLinkedListable<ktl::unique_ptr<FutexState>> {
   public:
    // PendingOpRef is a simple RAII helper meant to help management of the
    // pending operation count ref-counting in a FutexState object.  All of the
    // ways to fetch a FutexState from the free/active sets will return a
    // PendingOpRef to represent the borrow from the pool instead of a raw
    // FutexState pointer.  By default, these object will release a pending
    // operation reference when they go out of scope.  They do this under the
    // protection of the outer FutexContext's pool_lock_, returning the
    // FutexState to the FutexContext's free pool when the pending operation
    // count reaches zero.
    //
    // There are a few special extensions to the PendingOpRef added in order to
    // support some optimizations in the futex code paths.
    //
    // :: CancelRef ::
    // When a thread is about to block on a futex, it will have a PendingOpRef
    // in scope which is holding a pending operation reference to the
    // FutexState.  The thread which is about to block needs to keep that
    // reference on the FutexState as it sleeps, but it should never make an
    // attempt to remove the reference itself when it wakes up again.  There are
    // two reasons for this but the most important one is that, because of the
    // FutexRequeue operation, it may block on Futex A, but get moved over to
    // Futex B while blocking.  By the time it wakes up again, Futex A's
    // FutexState may no longer exist.
    //
    // When a thread has passed all of its checks and it is about to block, it
    // has entered the thread lock, and it uses CancelRef in order to cause its
    // PendingOpRef object to forget about the reference it is holding as it
    // blocks.
    //
    // :: SetExtraRefs ::
    // When a thread calls FutexWake, it will eventually enter the thread lock
    // in order to manipulate the targeted FutexState's wait queue.  For every
    // thread that it successfully wakes up from the wait queue, the wakeup
    // thread assumes responsibility for the woken thread's pending operation
    // reference.  This way, a thread which is successfully woken from a
    // FutexWake operation does not need to acquire any locks on its way out.
    // The thread which woke it up will release its pending operation reference
    // for it.  In order to account for these extra references, the waking
    // thread may call "SetExtraRefs" to account for the references that it took
    // responsibility for during the wake operation.
    //
    // Likewise, SetExtraRefs gets used on the slow path of a thread unblocking
    // from FutexWait.  In the case that a thread unblocks from FutexWait with
    // an error (timeout, thread killed, etc...), it will first wake up and find
    // it's FutexState using its blocking_futex_id_ member.  This may not be the
    // same futex that it originally blocked on.  Once the thread has found the
    // FutexState, it will be holding one pending operation reference as a
    // result of the find operation.  It needs to add another to account for the
    // pending operation reference it placed on the state when it originally
    // went to sleep.  It uses SetExtraRefs to accomplish this.
    //
    // :: TakeRefs ::
    // Finally, during a requeue operation, we are moving threads which are
    // currently blocked on futex A over to futex B.  As we do this, we need to
    // make sure to move their pending operation references at the same time.
    // TakeRefs is the method which allows us to do this.
    class PendingOpRef {
     public:
      PendingOpRef(FutexContext* ctx, FutexState* state) : ctx_(ctx), state_(state) {
        DEBUG_ASSERT(ctx_ != nullptr);
      }
      ~PendingOpRef() { Release(); }

      // RValue construction.
      PendingOpRef(PendingOpRef&& other) noexcept
          : ctx_(other.ctx_), state_(other.state_), extra_refs_(other.extra_refs_) {
        other.state_ = nullptr;
      }

      // No move assignment, and no copy of any form.  Also, no default
      // construction (although, this should be guaranteed by the lack of
      // inline-initializer for the const |ctx_| member.
      PendingOpRef() = delete;
      PendingOpRef(const PendingOpRef&) = delete;
      PendingOpRef& operator=(const PendingOpRef&) = delete;
      PendingOpRef& operator=(PendingOpRef&& other) = delete;

      void SetExtraRefs(uint32_t extra_refs) {
        DEBUG_ASSERT((extra_refs_ == 0) && (state_ != nullptr));
        extra_refs_ = extra_refs;
      }

      void TakeRefs(PendingOpRef* other, uint32_t count) {
        Guard<SpinLock, IrqSave> pool_lock_guard{&ctx_->pool_lock_};
        DEBUG_ASSERT(state_ != nullptr);
        DEBUG_ASSERT(other->state_ != nullptr);
        DEBUG_ASSERT(state_->pending_operation_count_ > 0);
        DEBUG_ASSERT(other->state_->pending_operation_count_ > count);

        state_->pending_operation_count_ += count;
        other->state_->pending_operation_count_ -= count;
      }

      void CancelRef() {
        DEBUG_ASSERT(state_ != nullptr);
        state_ = nullptr;
      }

      // Allow comparison against null, and dereferencing of the underlying state_ pointer.
      bool operator!=(nullptr_t) const { return (state_ != nullptr); }
      bool operator==(nullptr_t) const { return (state_ == nullptr); }
      const FutexState* operator->() const { return state_; }
      FutexState* operator->() { return state_; }

     private:
      void Release() {
        if (state_ != nullptr) {
          Guard<SpinLock, IrqSave> pool_lock_guard{&ctx_->pool_lock_};
          uint32_t release_count = 1 + extra_refs_;

          DEBUG_ASSERT(state_ != nullptr);
          DEBUG_ASSERT(state_->id() != 0);
          DEBUG_ASSERT(state_->pending_operation_count_ >= release_count);

          state_->pending_operation_count_ -= release_count;
          if (state_->pending_operation_count_ == 0) {
            ctx_->free_futexes_.push_front(ctx_->active_futexes_.erase(*state_));
            state_->id_ = 0;
            state_->waiters_.AssertNotOwned();
          }

          state_ = nullptr;
        }
      }

      // A PendingOpRef is a stack-only construct which exists within the scope
      // of a single FutexContext.  There is no reason why this value ever needs
      // to change over the life of the op-ref.
      FutexContext* const ctx_;
      FutexState* state_ = nullptr;
      uint32_t extra_refs_ = 0;
    };

    uintptr_t id() const { return id_; }

    // hashtable support
    uintptr_t GetKey() const { return id(); }
    static size_t GetHash(uintptr_t key) { return (key >> 3); }

   private:
    friend typename ktl::unique_ptr<FutexState>::deleter_type;
    friend class FutexContext;

    FutexState() = default;
    ~FutexState();

    FutexState(const FutexState&) = delete;
    FutexState(FutexState&&) = delete;
    FutexState& operator=(const FutexState&) = delete;
    FutexState& operator=(FutexState&&) = delete;

    uint32_t pending_operation_count() const { return pending_operation_count_; }

    uintptr_t id_ = 0;
    OwnedWaitQueue waiters_;

    // pending operation count is protected by the outer FutexContext pool lock.
    // Sadly, there is no good way to express this using static annotations.
    uint32_t pending_operation_count_ = 0;

    DECLARE_MUTEX(FutexContext) lock_ TA_ACQ_BEFORE(thread_lock);
  };

  // Definition of two small callback hooks used with OwnedWaitQueue::Wake and
  // OwnedWaitQueue::WakeAndRequeue.  These hooks perform two jobs.
  //
  // 1) They allow us to count the number of threads actually woken or requeued
  //    during these operations.  This is needed for proper pending op reference
  //    bookkeeping.
  //
  // 2) Second, they allow us to maintain user-thread blocked_futex_id info as
  //    the OwnedWaitQueue code selects threads to be woken/requeued.
  template <OwnedWaitQueue::Hook::Action action>
  static OwnedWaitQueue::Hook::Action ResetBlockingFutexId(Thread* thrd,
                                                           void* ctx) TA_NO_THREAD_SAFETY_ANALYSIS;
  template <OwnedWaitQueue::Hook::Action action>
  static OwnedWaitQueue::Hook::Action SetBlockingFutexId(Thread* thrd,
                                                         void* ctx) TA_NO_THREAD_SAFETY_ANALYSIS;

  // FutexContexts may not be copied, moved, or allocated on the heap.  They
  // are to exist as singleton members of the ProcessDispatcher class and
  // exist nowhere else in the system.
  FutexContext(const FutexContext&) = delete;
  FutexContext& operator=(const FutexContext&) = delete;
  FutexContext(FutexContext&&) = delete;
  FutexContext& operator=(FutexContext&&) = delete;
  static void* operator new(size_t) = delete;
  static void* operator new[](size_t) = delete;

  // Find the futex state for a given ID in the futex table, increment its
  // pending operation reference count, and return an RAII helper which helps to
  // manage the pending operation references.
  FutexState::PendingOpRef FindActiveFutex(uintptr_t id) TA_EXCL(pool_lock_) {
    Guard<SpinLock, IrqSave> pool_lock_guard{&pool_lock_};
    return FindActiveFutexLocked(id);
  }

  FutexState::PendingOpRef FindActiveFutexLocked(uintptr_t id) TA_REQ(pool_lock_) {
    auto iter = active_futexes_.find(id);

    if (iter.IsValid()) {
      DEBUG_ASSERT(iter->pending_operation_count_ > 0);
      ++(iter->pending_operation_count_);
      return {this, &(*iter)};
    }

    return {this, nullptr};
  }

  // Find a futex with the specified ID, increment its pending_operation_count
  // and return it to the caller.  If the given futex ID is not currently
  // active, grab a free one and activate it.
  FutexState::PendingOpRef ActivateFutex(uintptr_t id) TA_EXCL(pool_lock_) {
    Guard<SpinLock, IrqSave> pool_lock_guard{&pool_lock_};
    return ActivateFutexLocked(id);
  }

  FutexState::PendingOpRef ActivateFutexLocked(uintptr_t id) TA_REQ(pool_lock_) {
    if (auto ret = FindActiveFutexLocked(id); ret != nullptr) {
      return ret;
    }

    ktl::unique_ptr<FutexState> new_state = free_futexes_.pop_front();

    // Sanity checks.
    DEBUG_ASSERT(new_state != nullptr);
    DEBUG_ASSERT(new_state->id() == 0);
    DEBUG_ASSERT(new_state->pending_operation_count_ == 0);
    new_state->waiters_.AssertNotOwned();

    FutexState* ptr = new_state.get();
    ptr->id_ = id;
    ++ptr->pending_operation_count_;
    active_futexes_.insert(ktl::move(new_state));

    return {this, ptr};
  }

  // Protects the free futex pool, and active futex table.  This is an
  // irq-disable spin lock because it should _never_ be held during any blocking
  // operations.  Only when putting FutexStates into and out of the free pool,
  // and when moving Futexes states to and from the active table.
  //
  // There are times where an individual futex state must be held invariant
  // while a decision to return a futex into the free pool needs to be made.  In
  // these cases, the pool lock must be acquired *after* the individual
  // FutexState lock.  Sadly, I don't know a good way to express this with
  // static analysis.
  //
  // Note that lockdep tracking is disabled on this lock because it is acquired
  // while holding the thread lock.
  DECLARE_SPINLOCK(FutexContext, lockdep::LockFlagsTrackingDisabled) pool_lock_;

  // Hash table for FutexStates currently in use (eg; futexes with waiters).
  fbl::HashTable<uintptr_t, ktl::unique_ptr<FutexState>,
                 fbl::DoublyLinkedList<ktl::unique_ptr<FutexState>>>
      active_futexes_ TA_GUARDED(pool_lock_);

  // Free list for all futexes which are currently not in use.
  fbl::DoublyLinkedList<ktl::unique_ptr<FutexState>> free_futexes_ TA_GUARDED(pool_lock_);
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_FUTEX_CONTEXT_H_
