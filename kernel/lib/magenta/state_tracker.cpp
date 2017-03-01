// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/state_tracker.h>

#include <kernel/auto_lock.h>
#include <magenta/wait_event.h>

namespace {

template <typename Func>
void CancelWithFunc(StateTracker::ObserverList* observers, Mutex* observer_lock, Func f) {
    bool awoke_threads = false;

    StateTracker::ObserverList obs_to_remove;

    {
        AutoLock lock(observer_lock);
        for (auto it = observers->begin(); it != observers->end();) {
            awoke_threads = f(it.CopyPointer()) || awoke_threads;
            if (it->remove()) {
                auto to_remove = it;
                ++it;
                obs_to_remove.push_back(observers->erase(to_remove));
            } else {
                ++it;
            }
        }
    }

    while (!obs_to_remove.is_empty()) {
        obs_to_remove.pop_front()->OnRemoved();
    }

    if (awoke_threads)
        thread_preempt(false);
}
}  // namespace

void StateTracker::AddObserver(StateObserver* observer) {
    DEBUG_ASSERT(observer != nullptr);

    bool awoke_threads = false;
    {
        AutoLock lock(&lock_);

        awoke_threads = observer->OnInitialize(signals_);
        if (!observer->remove())
            observers_.push_front(observer);
    }
    if (awoke_threads)
        thread_preempt(false);
}

void StateTracker::RemoveObserver(StateObserver* observer) {
    AutoLock lock(&lock_);
    DEBUG_ASSERT(observer != nullptr);
    observers_.erase(*observer);
}

void StateTracker::Cancel(Handle* handle) {
    CancelWithFunc(&observers_, &lock_, [handle](StateObserver* obs) {
        return obs->OnCancel(handle);
    });
}

void StateTracker::CancelByKey(Handle* handle, uint64_t key) {
    CancelWithFunc(&observers_, &lock_, [handle, key](StateObserver* obs) {
        return obs->OnCancelByKey(handle, key);
    });
}

void StateTracker::UpdateState(mx_signals_t clear_mask,
                               mx_signals_t set_mask) {
    bool awoke_threads = false;

    ObserverList obs_to_remove;

    {
        AutoLock lock(&lock_);

        auto previous_signals = signals_;
        signals_ &= ~clear_mask;
        signals_ |= set_mask;

        if (previous_signals == signals_)
            return;

        for (auto it = observers_.begin(); it != observers_.end();) {
            awoke_threads = it->OnStateChange(signals_) || awoke_threads;
            if (it->remove()) {
                auto to_remove = it;
                ++it;
                obs_to_remove.push_back(observers_.erase(to_remove));
            } else {
                ++it;
            }
        }
    }

    while (!obs_to_remove.is_empty()) {
        obs_to_remove.pop_front()->OnRemoved();
    }

    if (awoke_threads) {
        thread_preempt(false);
    }
}
