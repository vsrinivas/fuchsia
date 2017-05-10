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

void StateTracker::AddObserver(StateObserver* observer, const StateObserver::CountInfo* cinfo) {
    canary_.Assert();
    DEBUG_ASSERT(observer != nullptr);

    bool awoke_threads = false;
    {
        AutoLock lock(&lock_);

        awoke_threads = observer->OnInitialize(signals_, cinfo);
        if (!observer->remove())
            observers_.push_front(observer);
    }
    if (awoke_threads)
        thread_preempt(false);
}

void StateTracker::RemoveObserver(StateObserver* observer) {
    canary_.Assert();

    AutoLock lock(&lock_);
    DEBUG_ASSERT(observer != nullptr);
    observers_.erase(*observer);
}

void StateTracker::Cancel(Handle* handle) {
    canary_.Assert();

    CancelWithFunc(&observers_, &lock_, [handle](StateObserver* obs) {
        return obs->OnCancel(handle);
    });
}

void StateTracker::CancelByKey(Handle* handle, const void* port, uint64_t key) {
    canary_.Assert();

    CancelWithFunc(&observers_, &lock_, [handle, port, key](StateObserver* obs) {
        return obs->OnCancelByKey(handle, port, key);
    });
}

void StateTracker::UpdateState(mx_signals_t clear_mask,
                               mx_signals_t set_mask) {
    canary_.Assert();

    bool awoke_threads;
    ObserverList obs_to_remove;

    {
        AutoLock lock(&lock_);
        auto previous_signals = signals_;
        signals_ &= ~clear_mask;
        signals_ |= set_mask;

        if (previous_signals == signals_)
            return;

        awoke_threads = UpdateInternalLocked(&obs_to_remove, signals_);
    }

    while (!obs_to_remove.is_empty()) {
        obs_to_remove.pop_front()->OnRemoved();
    }

    if (awoke_threads)
        thread_preempt(false);
}

void StateTracker::StrobeState(mx_signals_t notify_mask) {
    canary_.Assert();

    bool awoke_threads;
    ObserverList obs_to_remove;

    {
        AutoLock lock(&lock_);
        // include currently active signals as well
        notify_mask |= signals_;
        awoke_threads = UpdateInternalLocked(&obs_to_remove, notify_mask);
    }

    while (!obs_to_remove.is_empty()) {
        obs_to_remove.pop_front()->OnRemoved();
    }

    if (awoke_threads)
        thread_preempt(false);
}

void StateTracker::UpdateLastHandleSignal(uint32_t* count) {
    canary_.Assert();

    if (count == nullptr)
        return;

    bool awoke_threads;
    ObserverList obs_to_remove;

    {
        AutoLock lock(&lock_);

        auto previous_signals = signals_;

        // We assume here that the value pointed by |count| can mutate by
        // other threads.
        signals_ = (*count == 1u) ?
            signals_ | MX_SIGNAL_LAST_HANDLE : signals_ & ~MX_SIGNAL_LAST_HANDLE;

        if (previous_signals == signals_)
            return;

        awoke_threads = UpdateInternalLocked(&obs_to_remove, signals_);
    }

    while (!obs_to_remove.is_empty()) {
        obs_to_remove.pop_front()->OnRemoved();
    }

    if (awoke_threads)
        thread_preempt(false);
}

mx_status_t StateTracker::SetCookie(CookieJar* cookiejar, mx_koid_t scope, uint64_t cookie) {
    if (cookiejar == nullptr)
        return ERR_NOT_SUPPORTED;

    AutoLock lock(&lock_);

    if (cookiejar->scope_ == MX_KOID_INVALID) {
        cookiejar->scope_ = scope;
        cookiejar->cookie_ = cookie;
        return NO_ERROR;
    }

    if (cookiejar->scope_ == scope) {
        cookiejar->cookie_ = cookie;
        return NO_ERROR;
    }

    return ERR_ACCESS_DENIED;
}

mx_status_t StateTracker::GetCookie(CookieJar* cookiejar, mx_koid_t scope, uint64_t* cookie) {
    if (cookiejar == nullptr)
        return ERR_NOT_SUPPORTED;

    AutoLock lock(&lock_);

    if (cookiejar->scope_ == scope) {
        *cookie = cookiejar->cookie_;
        return NO_ERROR;
    }

    return ERR_ACCESS_DENIED;
}

mx_status_t StateTracker::InvalidateCookie(CookieJar* cookiejar) {
    if (cookiejar == nullptr)
        return ERR_NOT_SUPPORTED;

    AutoLock lock(&lock_);

    cookiejar->scope_ = MX_KOID_KERNEL;
    return NO_ERROR;
}

bool StateTracker::UpdateInternalLocked(ObserverList* obs_to_remove, mx_signals_t signals) {
    bool awoke_threads = false;

    for (auto it = observers_.begin(); it != observers_.end();) {
        awoke_threads = it->OnStateChange(signals) || awoke_threads;
        if (it->remove()) {
            auto to_remove = it;
            ++it;
            obs_to_remove->push_back(observers_.erase(to_remove));
        } else {
            ++it;
        }
    }
    return awoke_threads;
}
