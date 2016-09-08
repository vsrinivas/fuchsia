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

#include <mxtl/intrusive_double_list.h>
#include <mxtl/intrusive_wavl_tree.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/unique_ptr.h>

class WaitSetDispatcher final : public Dispatcher, public StateObserver {
public:
    // A wait set entry. It is always in the tree |entries_| (which owns it) and it is sometimes in
    // the doubly-linked list |triggered_entries_|.
    class Entry final : public StateObserver {
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

        struct TriggeredEntriesListTraits {
            static mxtl::DoublyLinkedListNodeState<Entry*>& node_state(Entry& obj) {
                return obj.triggered_entries_node_state_;
            }
        };

        using WAVLTreePtrType = mxtl::unique_ptr<Entry>;
        struct WAVLTreeNodeTraits {
            static mxtl::WAVLTreeNodeState<WAVLTreePtrType>& node_state(Entry& obj) {
                return obj.wavl_node_state_;
            }
        };

        static status_t Create(mx_signals_t watched_signals,
                               uint64_t cookie,
                               mxtl::unique_ptr<Entry>* entry);

        ~Entry();

        // Const, hence these don't care about locking:
        mx_signals_t watched_signals() const { return watched_signals_; }

        void Init_NoLock(WaitSetDispatcher* wait_set, Handle* handle);
        State GetState_NoLock() const;
        void SetState_NoLock(State new_state);
        Handle* GetHandle_NoLock() const;
        const mxtl::RefPtr<Dispatcher>& GetDispatcher_NoLock() const;
        bool IsTriggered_NoLock() const;
        mx_signals_state_t GetSignalsState_NoLock() const;

        bool InTriggeredEntriesList_NoLock() const {
            return triggered_entries_node_state_.InContainer();
        }

        // Used to be in the |entries_| tree.
        uint64_t GetKey() const { return cookie_; }

    private:
        Entry(mx_signals_t watched_signals, uint64_t cookie);
        Entry(const Entry&) = delete;
        Entry& operator=(const Entry&) = delete;

        bool OnInitialize(mx_signals_state_t initial_state) final;
        bool OnStateChange(mx_signals_state_t new_state) final;
        bool OnCancel(Handle* handle, bool* should_remove, bool* call_uninitialize) final;
        void OnDidCancel() final {}

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
        mxtl::RefPtr<Dispatcher> dispatcher_;

        bool is_triggered_ = false;
        mx_signals_state_t signals_state_ = {0u, 0u};

        mxtl::DoublyLinkedListNodeState<Entry*> triggered_entries_node_state_;
        mxtl::WAVLTreeNodeState<WAVLTreePtrType> wavl_node_state_;
    };

    static status_t Create(mxtl::RefPtr<Dispatcher>* dispatcher, mx_rights_t* rights);

    ~WaitSetDispatcher() final;
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_WAIT_SET; }
    StateTracker* get_state_tracker() final { return &state_tracker_; }

    // Adds an entry (previously constructed using Entry::Create()) for the given handle.
    // Note: Since this takes a handle, it should be called under the handle table lock!
    status_t AddEntry(mxtl::unique_ptr<Entry> entry, Handle* handle);

    // Removes an entry (previously added using AddEntry()).
    status_t RemoveEntry(uint64_t cookie);

    // Waits on the wait set. Note: This blocks.
    status_t Wait(mx_time_t timeout,
                  uint32_t* num_results,
                  mx_waitset_result_t* results,
                  uint32_t* max_results);

private:
    using WAVLTreePtrType = Entry::WAVLTreePtrType;
    using WAVLTreeKeyTraits = mxtl::DefaultKeyedObjectTraits<uint64_t, Entry>;
    using WAVLTreeNodeTraits = Entry::WAVLTreeNodeTraits;

    WaitSetDispatcher();

    WaitSetDispatcher(const WaitSetDispatcher&) = delete;
    WaitSetDispatcher& operator=(const WaitSetDispatcher&) = delete;

    // StateObserver implementation:
    bool OnInitialize(mx_signals_state_t initial_state) final;
    bool OnStateChange(mx_signals_state_t new_state) final;
    bool OnCancel(Handle* handle, bool* should_remove, bool* call_did_cancel) final;
    void OnDidCancel() final {}

    // These do the wait on |cv_|. They do *not* check the condition first.
    status_t DoWaitInfinite_NoLock();
    status_t DoWaitTimeout_NoLock(lk_time_t timeout);

    // We are *not* waitable, but we need to observe handle "cancellation".
    NonIrqStateTracker state_tracker_;

    // WARNING: No other locks may be taken under |mutex_|.
    Mutex mutex_;  // Protects the following members.

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

    mxtl::WAVLTree<uint64_t, WAVLTreePtrType, WAVLTreeKeyTraits, WAVLTreeNodeTraits> entries_;
    mxtl::DoublyLinkedList<Entry*, Entry::TriggeredEntriesListTraits> triggered_entries_;
    uint32_t num_triggered_entries_ = 0u;
};
