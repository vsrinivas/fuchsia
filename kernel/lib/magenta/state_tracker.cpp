// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/state_tracker.h>

#include <kernel/auto_lock.h>
#include <magenta/wait_event.h>

StateTracker::StateTracker(bool is_waitable, mx_signals_state_t signals_state)
    : is_waitable_(is_waitable),
      signals_state_(signals_state) {
    mutex_init(&lock_);
}

StateTracker::~StateTracker() {
    mutex_destroy(&lock_);
}

mx_status_t StateTracker::AddObserver(StateObserver* observer) {
    bool awoke_threads = false;
    {
        AutoLock lock(&lock_);

        observers_.push_front(observer);
        awoke_threads = observer->OnInitialize(signals_state_);
    }
    if (awoke_threads)
        thread_yield();
    return NO_ERROR;
}

mx_signals_state_t StateTracker::RemoveObserver(StateObserver* observer) {
    AutoLock lock(&lock_);
    DEBUG_ASSERT(observer != nullptr);
    observers_.erase(*observer);
    return signals_state_;
}

void StateTracker::UpdateState(mx_signals_t satisfied_clear_mask,
                               mx_signals_t satisfied_set_mask,
                               mx_signals_t satisfiable_clear_mask,
                               mx_signals_t satisfiable_set_mask) {
    bool awoke_threads = false;
    {
        AutoLock lock(&lock_);

        auto previous_signals_state = signals_state_;
        signals_state_.satisfied &= ~satisfied_clear_mask;
        signals_state_.satisfied |= satisfied_set_mask;
        signals_state_.satisfiable &= ~satisfiable_clear_mask;
        signals_state_.satisfiable |= satisfiable_set_mask;

        if (previous_signals_state.satisfied == signals_state_.satisfied &&
            previous_signals_state.satisfiable == signals_state_.satisfiable)
            return;

        for (auto& observer : observers_) {
            awoke_threads = observer.OnStateChange(signals_state_) || awoke_threads;
        }

    }
    if (awoke_threads)
        thread_yield();
}

void StateTracker::Cancel(Handle* handle) {
    bool awoke_threads = false;
    StateObserver* observer = nullptr;

    mxtl::DoublyLinkedList<StateObserver*, StateObserverListTraits> did_cancel_list;

    {
        AutoLock lock(&lock_);
        for (auto it = observers_.begin(); it != observers_.end();) {
            bool should_remove = false;
            bool call_did_cancel = false;
            awoke_threads = it->OnCancel(handle, &should_remove, &call_did_cancel) || awoke_threads;
            if (should_remove) {
                auto to_remove = it;
                ++it;
                observer = observers_.erase(to_remove);
                if (call_did_cancel)
                    did_cancel_list.push_front(observer);
            } else {
                ++it;
            }
        }
    }

    while (!did_cancel_list.is_empty()) {
        auto observer = did_cancel_list.pop_front();
        observer->OnDidCancel();
    }

    if (awoke_threads)
        thread_yield();
}
