// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/state_tracker.h>

#include <kernel/auto_lock.h>
#include <magenta/wait_event.h>

mx_status_t StateTracker::AddObserver(StateObserver* observer) {
    DEBUG_ASSERT(observer != nullptr);

    bool awoke_threads = false;
    {
        AutoLock lock(&lock_);

        observers_.push_front(observer);
        awoke_threads = observer->OnInitialize(signals_);
    }
    if (awoke_threads)
        thread_preempt(false);
    return NO_ERROR;
}

void StateTracker::RemoveObserver(StateObserver* observer) {
    AutoLock lock(&lock_);
    DEBUG_ASSERT(observer != nullptr);
    observers_.erase(*observer);
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
        if (observer)
            observer->OnDidCancel();
    }

    if (awoke_threads)
        thread_preempt(false);
}

void StateTracker::UpdateState(mx_signals_t clear_mask,
                               mx_signals_t set_mask) {
    bool awoke_threads = false;

    {
        AutoLock lock(&lock_);

        auto previous_signals = signals_;
        signals_ &= ~clear_mask;
        signals_ |= set_mask;

        if (previous_signals == signals_)
            return;

        for (auto& observer : observers_) {
            awoke_threads = observer.OnStateChange(signals_) || awoke_threads;
        }
    }

    if (awoke_threads) {
        thread_preempt(false);
    }
}
