// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/mutex.h>
#include <fbl/ref_ptr.h>
#include <kernel/lockdep.h>
#include <kernel/owned_wait_queue.h>
#include <ktl/move.h>
#include <ktl/unique_ptr.h>
#include <lib/user_copy/user_ptr.h>
#include <zircon/types.h>

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
    // that there is always one FutexState for each ThreadDispatcher in a
    // process.  This ensures that a thread which needs to wait on a futex can
    // always do so, since a futex with waiters requires one futex state, and
    // there can be at most N futexes with waiters, where N is the number of
    // threads in a process
    zx_status_t GrowFutexStatePool();
    void ShrinkFutexStatePool();

    // FutexWait first verifies that the integer pointed to by |value_ptr|
    // still equals |current_value|. If the test fails, FutexWait returns FAILED_PRECONDITION.
    // Otherwise it will block the current thread until the |deadline| passes, or until the thread
    // is woken by a FutexWake or FutexRequeue operation on the same |value_ptr| futex.
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

    // FutexWait first verifies that the integer pointed to by |wake_ptr|
    // still equals |current_value|. If the test fails, FutexWait returns FAILED_PRECONDITION.
    // Otherwise it will wake up to |wake_count| number of threads blocked on the |wake_ptr| futex.
    // If any other threads remain blocked on on the |wake_ptr| futex, up to |requeue_count|
    // of them will then be requeued to the tail of the list of threads
    // blocked on the |requeue_ptr| futex.
    //
    // If owner_action is set to RELEASE, then the futex's owner will be set to nullptr in the
    // process.  If the owner_action is set to ASSIGN_WOKEN, then the wake_count *must* be 1, and
    // the futex's owner will be set to the thread which was woken during the operation, or nullptr
    // if no thread was woken.
    zx_status_t FutexRequeue(user_in_ptr<const zx_futex_t> wake_ptr,
                             uint32_t wake_count,
                             zx_futex_t current_value,
                             OwnerAction owner_action,
                             user_in_ptr<const zx_futex_t> requeue_ptr,
                             uint32_t requeue_count,
                             zx_handle_t new_requeue_owner);

    // Get the KOID of the current owner of the specified futex, if any, or ZX_KOID_INVALID if there
    // is no known owner.
    zx_status_t FutexGetOwner(user_in_ptr<const zx_futex_t> value_ptr,
                              user_out_ptr<zx_koid_t> koid);

private:
    // Notes about FutexState lifecycle.
    // aka. Why is this safe?
    //
    // FutexState objects are used to track the state of any futex which
    // currently has waiters.  Currently, each thread in a process allocates one
    // FutexState and contributes its process' futex context's free pool.  When
    // the thread exits, it takes one context out of the free pool and lets it
    // expire.  A upper bound for the maximum number of active FutexStates in a
    // process is the current number of threads in the system, because to be
    // active, a FutexState needs to have at least one waiter.  So, by ensuring
    // that each thread contributes one FutexState to the process' pool, we can
    // be sure that we will always have at least one FutexState in the free pool
    // when it comes time for a thread to wait on a currently uncontested futex.
    //
    // FutexState objects are managed using ktl::unique_ptr.  At all times, a
    // FutexState will be in one of three states.
    //
    // 1) A member of a FutexContext's futex_table_.  Futexes in this state are
    //    currently active and have waiters.  Their futex ID will be non-zero.
    // 2) A member of a FutexContext's free_futexes_ list.  These futexes are
    //    not currently in use, but are available to be allocated and used.
    //    Their futex ID will be zero.
    // 3) A member of neither.  These futexes have been created, but not added
    //    to the pool yet, or removed from the free list by a thread which is
    //    exiting.  Their futex ID will be zero.
    //
    // During operation, FutexStates are borrowed from the active pool using
    // either |ObtainActiveFutex| or |ActivateFromPool| and held as a raw
    // FutexState*.  This is done under the protection of the FutexContex lock_,
    // and the life cycle of any FutexState* retrieved this way must never be
    // allowed to leave the scope in which lock_ is held as this reference has
    // only been borrowed, and it could become invalid as soon as the lock has
    // been released.
    //
    // TODO(johngro): Investigate more rigorous ways to enforce this borrow
    // pattern.  Introducing a move-only pointer wrapper object returned from
    // |ObtainActiveFutex| and |ActivateFromPool|, and given back during
    // |ReturnFromPool| could do the job if its constructor/destructor could be
    // made to TA_REQ the FutexContex::lock_, but unfortunately I know of no
    // good way to actually do this using the clang static analysis tools.
    //
    class FutexState : public fbl::DoublyLinkedListable<ktl::unique_ptr<FutexState>> {
    public:
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
        FutexState& operator=(const FutexState&) = delete;

        uintptr_t id_ = 0;
        OwnedWaitQueue waiters_;
    };

    // Definition of a small callback hook used with OwnedWaitQueue::Wake and
    // OwnedWaitQueue::WakeAndRequeue in order to allow us to maintain user
    // thread blocked futex ID info as the OwnedWaitQueue code selects threads
    // to be woken/requeued.
    template <OwnedWaitQueue::Hook::Action action>
    static OwnedWaitQueue::Hook::Action SetBlockingFutexId(thread_t* thrd, void* ctx)
    TA_NO_THREAD_SAFETY_ANALYSIS;

    // FutexContexts may not be copied, moved, or allocated on the heap.  They
    // are to exist as singleton members of the ProcessDispatcher class and
    // exist nowhere else in the system.
    FutexContext(const FutexContext&) = delete;
    FutexContext& operator=(const FutexContext&) = delete;
    FutexContext(FutexContext&&) = delete;
    FutexContext& operator=(FutexContext&&) = delete;
    static void* operator new(size_t) = delete;
    static void* operator new[](size_t) = delete;

    // Find a the futex state for a given ID in the futex table and return a raw
    // (borrowed) pointer to it, or nullptr if there is no such ID in the table.
    FutexState* ObtainActiveFutex(uintptr_t id) TA_REQ(lock_) {
        auto iter = futex_table_.find(id);
        return iter.IsValid() ? &(*iter) : nullptr;
    }

    // Take a futex from the free pool and add it to the futex table, assigning
    // its new ID in the process.  Returns a raw pointer to the FutexState which
    // was activated.
    FutexState* ActivateFromPool(uintptr_t id) TA_REQ(lock_) {
        ktl::unique_ptr<FutexState> new_state = free_futexes_.pop_front();
        FutexState* ret = new_state.get();

        DEBUG_ASSERT(new_state != nullptr);
        DEBUG_ASSERT(new_state->id() == 0);
        new_state->waiters_.AssertNotOwned();

        new_state->id_ = id;
        futex_table_.insert(ktl::move(new_state));
        return ret;
    }

    // Return a futex which is currently in the futex hash table to the free
    // pool.  Note, any owner of the wait queue must have already been released by now.
    void ReturnToPool(FutexState* futex) TA_REQ(lock_) {
        DEBUG_ASSERT(futex != nullptr);
        DEBUG_ASSERT(futex->id() != 0);
        DEBUG_ASSERT(futex->InContainer());
        futex->waiters_.AssertNotOwned();

        free_futexes_.push_front(futex_table_.erase(*futex));
        futex->id_ = 0;
    }

    // Protects the futex_table_.  Must be held before acquiring the thread lock.
    DECLARE_MUTEX(FutexContext) lock_ TA_ACQ_BEFORE(thread_lock);

    // Hash table for FutexStates currently in use (eg; futexes with waiters).
    fbl::HashTable<uintptr_t,
                   ktl::unique_ptr<FutexState>,
                   fbl::DoublyLinkedList<ktl::unique_ptr<FutexState>>> futex_table_
                       TA_GUARDED(lock_);

    // Free list for all futexes which are currently not in use.
    fbl::DoublyLinkedList<ktl::unique_ptr<FutexState>> free_futexes_ TA_GUARDED(lock_);
};
