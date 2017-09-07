// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/state_tracker.h>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

using fbl::AutoLock;

namespace {

template <typename Func>
StateObserver::Flags CancelWithFunc(StateTracker::ObserverList* observers,
                                    fbl::Mutex* observer_lock, Func f) {
    StateObserver::Flags flags = 0;

    StateTracker::ObserverList obs_to_remove;

    {
        AutoLock lock(observer_lock);
        for (auto it = observers->begin(); it != observers->end();) {
            StateObserver::Flags it_flags = f(it.CopyPointer());
            flags |= it_flags;
            if (it_flags & StateObserver::kNeedRemoval) {
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

    // We've processed the removal flag, so strip it
    return flags & (~StateObserver::kNeedRemoval);
}

}  // namespace

void StateTracker::AddObserver(StateObserver* observer, const StateObserver::CountInfo* cinfo) {
    canary_.Assert();
    DEBUG_ASSERT(observer != nullptr);

    StateObserver::Flags flags;
    {
        AutoLock lock(&lock_);

        flags = observer->OnInitialize(signals_, cinfo);
        if (!(flags & StateObserver::kNeedRemoval))
            observers_.push_front(observer);
    }
    if (flags & StateObserver::kNeedRemoval)
        observer->OnRemoved();
    if (flags & StateObserver::kWokeThreads)
        thread_reschedule();
}

void StateTracker::RemoveObserver(StateObserver* observer) {
    canary_.Assert();

    AutoLock lock(&lock_);
    DEBUG_ASSERT(observer != nullptr);
    observers_.erase(*observer);
}

bool StateTracker::Cancel(Handle* handle) {
    canary_.Assert();

    StateObserver::Flags flags = CancelWithFunc(&observers_, &lock_, [handle](StateObserver* obs) {
        return obs->OnCancel(handle);
    });

    // We could request a reschedule if kWokeThreads is asserted,
    // but cancellation is not likely to benefit from aggressive
    // rescheduling.
    return flags & StateObserver::kHandled;
}

bool StateTracker::CancelByKey(Handle* handle, const void* port, uint64_t key) {
    canary_.Assert();

    StateObserver::Flags flags = CancelWithFunc(&observers_, &lock_, [handle, port, key](StateObserver* obs) {
        return obs->OnCancelByKey(handle, port, key);
    });

    // We could request a reschedule if kWokeThreads is asserted,
    // but cancellation is not likely to benefit from aggressive
    // rescheduling.
    return flags & StateObserver::kHandled;
}

void StateTracker::UpdateState(mx_signals_t clear_mask,
                               mx_signals_t set_mask) {
    canary_.Assert();

    StateObserver::Flags flags;
    ObserverList obs_to_remove;

    {
        AutoLock lock(&lock_);
        auto previous_signals = signals_;
        signals_ &= ~clear_mask;
        signals_ |= set_mask;

        if (previous_signals == signals_)
            return;

        flags = UpdateInternalLocked(&obs_to_remove, signals_);
    }

    while (!obs_to_remove.is_empty()) {
        obs_to_remove.pop_front()->OnRemoved();
    }

    if (flags & StateObserver::kWokeThreads)
        thread_reschedule();
}

void StateTracker::UpdateLastHandleSignal(uint32_t* count) {
    canary_.Assert();

    if (count == nullptr)
        return;

    StateObserver::Flags flags = 0;
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

        flags = UpdateInternalLocked(&obs_to_remove, signals_);
    }

    while (!obs_to_remove.is_empty()) {
        obs_to_remove.pop_front()->OnRemoved();
    }

    if (flags & StateObserver::kWokeThreads)
        thread_reschedule();
}

mx_status_t StateTracker::SetCookie(CookieJar* cookiejar, mx_koid_t scope, uint64_t cookie) {
    if (cookiejar == nullptr)
        return MX_ERR_NOT_SUPPORTED;

    AutoLock lock(&lock_);

    if (cookiejar->scope_ == MX_KOID_INVALID) {
        cookiejar->scope_ = scope;
        cookiejar->cookie_ = cookie;
        return MX_OK;
    }

    if (cookiejar->scope_ == scope) {
        cookiejar->cookie_ = cookie;
        return MX_OK;
    }

    return MX_ERR_ACCESS_DENIED;
}

mx_status_t StateTracker::GetCookie(CookieJar* cookiejar, mx_koid_t scope, uint64_t* cookie) {
    if (cookiejar == nullptr)
        return MX_ERR_NOT_SUPPORTED;

    AutoLock lock(&lock_);

    if (cookiejar->scope_ == scope) {
        *cookie = cookiejar->cookie_;
        return MX_OK;
    }

    return MX_ERR_ACCESS_DENIED;
}

mx_status_t StateTracker::InvalidateCookie(CookieJar* cookiejar) {
    if (cookiejar == nullptr)
        return MX_ERR_NOT_SUPPORTED;

    AutoLock lock(&lock_);

    cookiejar->scope_ = MX_KOID_KERNEL;
    return MX_OK;
}

StateObserver::Flags StateTracker::UpdateInternalLocked(ObserverList* obs_to_remove, mx_signals_t signals) {
    StateObserver::Flags flags = 0;

    for (auto it = observers_.begin(); it != observers_.end();) {
        StateObserver::Flags it_flags = it->OnStateChange(signals);
        flags |= it_flags;
        if (it_flags & StateObserver::kNeedRemoval) {
            auto to_remove = it;
            ++it;
            obs_to_remove->push_back(observers_.erase(to_remove));
        } else {
            ++it;
        }
    }

    // Filter out NeedRemoval flag because we processed that here
    return flags & (~StateObserver::kNeedRemoval);
}
