// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/dispatcher.h>

#include <arch/ops.h>
#include <lib/ktrace.h>
#include <fbl/atomic.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

using fbl::AutoLock;

namespace {
// The first 1K koids are reserved.
fbl::atomic<zx_koid_t> global_koid(1024ULL);

zx_koid_t GenerateKernelObjectId() {
    return global_koid.fetch_add(1ULL);
}

}  // namespace

Dispatcher::Dispatcher(zx_signals_t signals)
    : koid_(GenerateKernelObjectId()),
      handle_count_(0u),
      signals_(signals) {
}

Dispatcher::~Dispatcher() {
#if WITH_LIB_KTRACE
    ktrace(TAG_OBJECT_DELETE, (uint32_t)koid_, 0, 0, 0);
#endif
}

zx_status_t Dispatcher::add_observer(StateObserver* observer) {
    if (!has_state_tracker())
        return ZX_ERR_NOT_SUPPORTED;
    AddObserver(observer, nullptr);
    return ZX_OK;
}

zx_status_t Dispatcher::user_signal(uint32_t clear_mask, uint32_t set_mask, bool peer) {
    if (peer)
        return ZX_ERR_NOT_SUPPORTED;

    if (!has_state_tracker())
        return ZX_ERR_NOT_SUPPORTED;

    // Generic objects can set all USER_SIGNALs. Particular object
    // types (events and eventpairs) may be able to set more.
    if ((set_mask & ~ZX_USER_SIGNAL_ALL) || (clear_mask & ~ZX_USER_SIGNAL_ALL))
        return ZX_ERR_INVALID_ARGS;

    UpdateState(clear_mask, set_mask);
    return ZX_OK;
}

namespace {

template <typename Func>
StateObserver::Flags CancelWithFunc(Dispatcher::ObserverList* observers,
                                    fbl::Mutex* observer_lock, Func f) {
    StateObserver::Flags flags = 0;

    Dispatcher::ObserverList obs_to_remove;

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

void Dispatcher::AddObserver(StateObserver* observer, const StateObserver::CountInfo* cinfo) {
    ZX_DEBUG_ASSERT(has_state_tracker());
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

void Dispatcher::RemoveObserver(StateObserver* observer) {
    ZX_DEBUG_ASSERT(has_state_tracker());

    AutoLock lock(&lock_);
    DEBUG_ASSERT(observer != nullptr);
    observers_.erase(*observer);
}

bool Dispatcher::Cancel(Handle* handle) {
    ZX_DEBUG_ASSERT(has_state_tracker());

    StateObserver::Flags flags = CancelWithFunc(&observers_, &lock_, [handle](StateObserver* obs) {
        return obs->OnCancel(handle);
    });

    // We could request a reschedule if kWokeThreads is asserted,
    // but cancellation is not likely to benefit from aggressive
    // rescheduling.
    return flags & StateObserver::kHandled;
}

bool Dispatcher::CancelByKey(Handle* handle, const void* port, uint64_t key) {
    ZX_DEBUG_ASSERT(has_state_tracker());

    StateObserver::Flags flags = CancelWithFunc(&observers_, &lock_, [handle, port, key](StateObserver* obs) {
        return obs->OnCancelByKey(handle, port, key);
    });

    // We could request a reschedule if kWokeThreads is asserted,
    // but cancellation is not likely to benefit from aggressive
    // rescheduling.
    return flags & StateObserver::kHandled;
}

// Since this conditionally takes the dispatcher's |lock_|, based on
// the type of Mutex (either fbl::Mutex or fbl::NullLock), the thread
// safety analysis is unable to prove that the accesses to |signals_|
// are always protected.
template <typename Mutex>
void Dispatcher::UpdateStateHelper(zx_signals_t clear_mask,
                                   zx_signals_t set_mask,
                                   Mutex* mutex) TA_NO_THREAD_SAFETY_ANALYSIS {
    StateObserver::Flags flags;
    Dispatcher::ObserverList obs_to_remove;

    {
        AutoLock lock(mutex);
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

void Dispatcher::UpdateState(zx_signals_t clear_mask,
                             zx_signals_t set_mask) {
    ZX_DEBUG_ASSERT(has_state_tracker());

    UpdateStateHelper(clear_mask, set_mask, &lock_);
}

void Dispatcher::UpdateStateLocked(zx_signals_t clear_mask,
                                   zx_signals_t set_mask) {
    ZX_DEBUG_ASSERT(has_state_tracker());

    fbl::NullLock mutex;
    UpdateStateHelper(clear_mask, set_mask, &mutex);
}

zx_status_t Dispatcher::SetCookie(CookieJar* cookiejar, zx_koid_t scope, uint64_t cookie) {
    ZX_DEBUG_ASSERT(has_state_tracker());

    if (cookiejar == nullptr)
        return ZX_ERR_NOT_SUPPORTED;

    AutoLock lock(&lock_);

    if (cookiejar->scope_ == ZX_KOID_INVALID) {
        cookiejar->scope_ = scope;
        cookiejar->cookie_ = cookie;
        return ZX_OK;
    }

    if (cookiejar->scope_ == scope) {
        cookiejar->cookie_ = cookie;
        return ZX_OK;
    }

    return ZX_ERR_ACCESS_DENIED;
}

zx_status_t Dispatcher::GetCookie(CookieJar* cookiejar, zx_koid_t scope, uint64_t* cookie) {
    ZX_DEBUG_ASSERT(has_state_tracker());

    if (cookiejar == nullptr)
        return ZX_ERR_NOT_SUPPORTED;

    AutoLock lock(&lock_);

    if (cookiejar->scope_ == scope) {
        *cookie = cookiejar->cookie_;
        return ZX_OK;
    }

    return ZX_ERR_ACCESS_DENIED;
}

zx_status_t Dispatcher::InvalidateCookie(CookieJar* cookiejar) {
    ZX_DEBUG_ASSERT(has_state_tracker());

    if (cookiejar == nullptr)
        return ZX_ERR_NOT_SUPPORTED;

    AutoLock lock(&lock_);

    cookiejar->scope_ = ZX_KOID_KERNEL;
    return ZX_OK;
}

StateObserver::Flags Dispatcher::UpdateInternalLocked(ObserverList* obs_to_remove, zx_signals_t signals) {
    ZX_DEBUG_ASSERT(has_state_tracker());

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
