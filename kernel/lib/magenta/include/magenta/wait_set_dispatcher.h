// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <kernel/cond.h>
#include <kernel/mutex.h>

#include <magenta/dispatcher.h>
#include <magenta/state_observer.h>
#include <magenta/state_tracker.h>

#include <sys/types.h>

#include <utils/hash_table.h>
#include <utils/intrusive_double_list.h>
#include <utils/intrusive_single_list.h>
#include <utils/ref_ptr.h>
#include <utils/unique_ptr.h>

class WaitSetDispatcher final : public Dispatcher, public StateObserver {
public:
    // A wait set entry. It may be in two linked lists: it is always in a singly-linked list in the
    // hash table |entries_| (which owns it) and it is sometimes in the doubly-linked list
    // |triggered_entries_|.
    // TODO(vtl): Make this use unique_ptr when HashTable supports that.
    class Entry final : public StateObserver,
                        public utils::SinglyLinkedListable<Entry*>,
                        public utils::DoublyLinkedListable<Entry*> {
    public:
        // State transitions:
        //   The normal cycle is:
        //     UNINITIALIZED -> ADD_PENDING -> ADDED -> REMOVED -> <destroyed>.
        //
        // The problem is that StateTracker methods cannot be called under the owning
        // WaitSetDispatcher's mutex. Thus AddEntry() initializes the state to ADD_PENDING under the
        // mutex and calls the StateTracker's AddObserver() outside the lock. AddObserver() will
        // call OnInitialize(), which reacquires the mutex and sets the state to ADDED. After
        // calling AddObserver(), AddEntry() returns immediately (assuming AddObserver() succeeded).
        //
        // RemoveEntry() may be called and observe the ADD_PENDING state (under the mutex). In that
        // case, it pretends that the entry hasn't been added (since AddEntry() hasn't been
        // completed) and just fails. Otherwise, it removes it and sets the state to REMOVE while
        // still under the mutex. It then releases the mutex and calls the StateTracker's
        // RemoveObserver().
        enum class State { UNINITIALIZED, ADD_PENDING, ADDED, REMOVED };

        static status_t Create(mx_signals_t watched_signals,
                               uint64_t cookie,
                               utils::unique_ptr<Entry>* entry);

        ~Entry();

        // Const, hence these don't care about locking:
        mx_signals_t watched_signals() const { return watched_signals_; }
        uint64_t cookie() const { return cookie_; }

        void Init_NoLock(WaitSetDispatcher* wait_set, Handle* handle);
        State GetState_NoLock() const;
        void SetState_NoLock(State new_state);
        Handle* GetHandle_NoLock() const;
        const utils::RefPtr<Dispatcher>& GetDispatcher_NoLock() const;
        bool IsTriggered_NoLock() const;
        mx_signals_state_t GetSignalsState_NoLock() const;

    private:
        Entry(mx_signals_t watched_signals, uint64_t cookie);
        Entry(const Entry&) = delete;
        Entry& operator=(const Entry&) = delete;

        bool OnInitialize(mx_signals_state_t initial_state) final;
        bool OnStateChange(mx_signals_state_t new_state) final;
        bool OnCancel(Handle* handle, bool* should_remove) final;

        // Triggers (including adding to the triggered list). It must not already be triggered
        // (i.e., |is_triggered_| must be false; this will set it to true).
        bool Trigger_NoLock();

        const mx_signals_t watched_signals_;
        const uint64_t cookie_;

        // The members below are all protected by the owning WaitSetDispatcher's mutex (once the
        // entry has an owner).

        State state_ = State::UNINITIALIZED;
        WaitSetDispatcher* wait_set_ = nullptr;
        Handle* handle_ = nullptr;
        // We mostly need |dispatcher|'s StateTracker, but we need a ref to keep it alive. (This may
        // be non-null even if |handle_| is null if |OnCancel()| has been called.)
        utils::RefPtr<Dispatcher> dispatcher_;

        bool is_triggered_ = false;
        mx_signals_state_t signals_state_ = {0u, 0u};
    };

    static status_t Create(utils::RefPtr<Dispatcher>* dispatcher, mx_rights_t* rights);

    ~WaitSetDispatcher() final;
    mx_obj_type_t GetType() const final { return MX_OBJ_TYPE_WAIT_SET; }
    WaitSetDispatcher* get_wait_set_dispatcher() final { return this; }

    StateTracker* get_state_tracker() final { return &state_tracker_; }

    // Adds an entry (previously constructed using Entry::Create()) for the given handle.
    // Note: Since this takes a handle, it should be called under the handle table lock!
    status_t AddEntry(utils::unique_ptr<Entry> entry, Handle* handle);

    // Removes an entry (previously added using AddEntry()).
    status_t RemoveEntry(uint64_t cookie);

    // Waits on the wait set. Note: This blocks.
    status_t Wait(mx_time_t timeout,
                  uint32_t* num_results,
                  mx_wait_set_result_t* results,
                  uint32_t* max_results);

private:
    static constexpr size_t kNumBuckets = 127u;
    struct CookieHashFn {
        uint64_t operator()(uint64_t n) const { return n; }
    };

    WaitSetDispatcher();

    WaitSetDispatcher(const WaitSetDispatcher&) = delete;
    WaitSetDispatcher& operator=(const WaitSetDispatcher&) = delete;

    // StateObserver implementation:
    bool OnInitialize(mx_signals_state_t initial_state) final;
    bool OnStateChange(mx_signals_state_t new_state) final;
    bool OnCancel(Handle* handle, bool* should_remove) final;

    // These do the wait on |cv_|. They do *not* check the condition first.
    status_t DoWaitInfinite_NoLock();
    status_t DoWaitTimeout_NoLock(lk_time_t timeout);

    // We are *not* waitable, but we need to observe handle "cancellation".
    StateTracker state_tracker_;

    // WARNING: No other locks may be taken under |mutex_|.
    mutex_t mutex_;  // Protects the following members.

    // Associated to |mutex_|. This should be signaled (broadcast) when |triggered_entries_| becomes
    // nonempty or |cancelled_| becomes set.
    cond_t cv_;
    // We count the number of waiters, so we can provide a slightly fake value about whether we
    // awoke anyone or not when we signal |cv_| (since |cond_broadcast()| doesn't tell us anything).
    size_t waiter_count_ = 0u;

    // Whether our (only) handle has been cancelled.
    // TODO(vtl): If we ever allow wait set handles to be duplicated, we'll have to do much more
    // complicated accounting, both in Wait() and in OnCancel().
    bool cancelled_ = false;

    utils::HashTable<uint64_t, Entry, CookieHashFn, kNumBuckets> entries_;
    utils::DoublyLinkedList<Entry*> triggered_entries_;
    uint32_t num_triggered_entries_ = 0u;
};

uint64_t GetHashTableKey(const WaitSetDispatcher::Entry* entry);
void SetHashTableKey(WaitSetDispatcher::Entry* entry, uint64_t key);
