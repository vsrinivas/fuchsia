// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/dispatcher.h>

#include <inttypes.h>

#include <arch/ops.h>
#include <lib/ktrace.h>
#include <lib/counters.h>
#include <fbl/atomic.h>
#include <fbl/mutex.h>

#include <object/tls_slots.h>


// kernel counters. The following counters never decrease.
// counts the number of times a dispatcher has been created and destroyed.
KCOUNTER(dispatcher_create_count, "kernel.dispatcher.create");
KCOUNTER(dispatcher_destroy_count, "kernel.dispatcher.destroy");
// counts the number of times observers have been added to a kernel object.
KCOUNTER(dispatcher_observe_count, "kernel.dispatcher.observer.add");
// counts the number of times observers have been canceled.
KCOUNTER(dispatcher_cancel_bh_count, "kernel.dispatcher.observer.cancel.byhandle");
KCOUNTER(dispatcher_cancel_bk_count, "kernel.dispatcher.observer.cancel.bykey");
// counts the number of cookies set or changed (reset).
KCOUNTER(dispatcher_cookie_set_count, "kernel.dispatcher.cookie.set");
KCOUNTER(dispatcher_cookie_reset_count, "kernel.dispatcher.cookie.reset");

namespace {
// The first 1K koids are reserved.
fbl::atomic<zx_koid_t> global_koid(1024ULL);

zx_koid_t GenerateKernelObjectId() {
    return global_koid.fetch_add(1ULL, fbl::memory_order_relaxed);
}

// Helper class that safely allows deleting Dispatchers without
// risk of blowing up the kernel stack. It uses one TLS slot to
// unwind the recursion.
class SafeDeleter {
public:
    static SafeDeleter* Get() {
        auto self = reinterpret_cast<SafeDeleter*>(tls_get(TLS_ENTRY_KOBJ_DELETER));
        if (self == nullptr) {
            fbl::AllocChecker ac;
            self = new (&ac) SafeDeleter;
            if (!ac.check())
                return nullptr;

            tls_set(TLS_ENTRY_KOBJ_DELETER, self);
            tls_set_callback(TLS_ENTRY_KOBJ_DELETER, &CleanTLS);
        }
        return self;
    }

    void Delete(Dispatcher* kobj) {
        if (level_ > 0) {
            pending_.push_front(kobj);
            return;
        }
        // The delete calls below can recurse here via fbl_recycle().
        level_++;
        delete kobj;

        while ((kobj = pending_.pop_front()) != nullptr) {
            delete kobj;
        }
        level_--;
    }

private:
    static void CleanTLS(void* tls) {
        delete reinterpret_cast<SafeDeleter*>(tls);
    }

    SafeDeleter() : level_(0) {}
    ~SafeDeleter() { DEBUG_ASSERT(level_ == 0); }

    int level_;
    fbl::SinglyLinkedList<Dispatcher*, Dispatcher::DeleterListTraits> pending_;
};

}  // namespace

Dispatcher::Dispatcher(zx_signals_t signals)
    : koid_(GenerateKernelObjectId()),
      handle_count_(0u),
      signals_(signals) {

    kcounter_add(dispatcher_create_count, 1);
}

Dispatcher::~Dispatcher() {
#if WITH_LIB_KTRACE
    ktrace(TAG_OBJECT_DELETE, (uint32_t)koid_, 0, 0, 0);
#endif
    kcounter_add(dispatcher_destroy_count, 1);
}

// The refcount of this object has reached zero: delete self
// using the SafeDeleter to avoid potential recursion hazards.
// TODO(cpu): Not all object need the SafeDeleter. Only objects
// that can control the lifetime of dispatchers that in turn
// can control the lifetime of others. For example events do
// not fall in this category.
void Dispatcher::fbl_recycle() {
    auto deleter = SafeDeleter::Get();
    if (likely(deleter != nullptr)) {
        deleter->Delete(this);
    } else {
        // We failed to allocate the safe deleter. As an OOM
        // case one is extremely unlikely but possible. Attempt
        // to delete the dispatcher directly which very likely
        // can be done without blowing the stack.
        delete this;
    }
}

zx_status_t Dispatcher::add_observer(StateObserver* observer) {
    if (!has_state_tracker())
        return ZX_ERR_NOT_SUPPORTED;
    AddObserver(observer, nullptr);
    return ZX_OK;
}

namespace {

template <typename Func, typename LockType>
StateObserver::Flags CancelWithFunc(Dispatcher::ObserverList* observers,
                                    Lock<LockType>* observer_lock, Func f) {
    StateObserver::Flags flags = 0;

    Dispatcher::ObserverList obs_to_remove;

    {
        Guard<LockType> guard{observer_lock};
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

// Since this conditionally takes the dispatcher's |lock_|, based on
// the type of Mutex (either fbl::Mutex or fbl::NullLock), the thread
// safety analysis is unable to prove that the accesses to |signals_|
// and to |observers_| are always protected.
template <typename LockType>
void Dispatcher::AddObserverHelper(StateObserver* observer,
                                   const StateObserver::CountInfo* cinfo,
                                   Lock<LockType>* lock) TA_NO_THREAD_SAFETY_ANALYSIS {
    ZX_DEBUG_ASSERT(has_state_tracker());
    DEBUG_ASSERT(observer != nullptr);

    StateObserver::Flags flags;
    {
        Guard<LockType> guard{lock};

        flags = observer->OnInitialize(signals_, cinfo);
        if (!(flags & StateObserver::kNeedRemoval))
            observers_.push_front(observer);
    }
    if (flags & StateObserver::kNeedRemoval)
        observer->OnRemoved();

    kcounter_add(dispatcher_observe_count, 1);
}

void Dispatcher::AddObserver(StateObserver* observer, const StateObserver::CountInfo* cinfo) {
    AddObserverHelper(observer, cinfo, get_lock());
}

void Dispatcher::AddObserverLocked(StateObserver* observer, const StateObserver::CountInfo* cinfo) {
    // Type tag and local NullLock to make lockdep happy.
    struct DispatcherAddObserverLocked {};
    DECLARE_LOCK(DispatcherAddObserverLocked, fbl::NullLock) lock;

    AddObserverHelper(observer, cinfo, &lock);
}

void Dispatcher::RemoveObserver(StateObserver* observer) {
    ZX_DEBUG_ASSERT(has_state_tracker());

    Guard<fbl::Mutex> guard{get_lock()};
    DEBUG_ASSERT(observer != nullptr);
    observers_.erase(*observer);
}

bool Dispatcher::Cancel(Handle* handle) {
    ZX_DEBUG_ASSERT(has_state_tracker());

    StateObserver::Flags flags = CancelWithFunc(&observers_, get_lock(),
                                                [handle](StateObserver* obs) {
        return obs->OnCancel(handle);
    });

    kcounter_add(dispatcher_cancel_bh_count, 1);

    return flags & StateObserver::kHandled;
}

bool Dispatcher::CancelByKey(Handle* handle, const void* port, uint64_t key) {
    ZX_DEBUG_ASSERT(has_state_tracker());

    StateObserver::Flags flags = CancelWithFunc(&observers_, get_lock(),
                                                [handle, port, key](StateObserver* obs) {
        return obs->OnCancelByKey(handle, port, key);
    });

    kcounter_add(dispatcher_cancel_bk_count, 1);

    return flags & StateObserver::kHandled;
}

// Since this conditionally takes the dispatcher's |lock_|, based on
// the type of Mutex (either fbl::Mutex or fbl::NullLock), the thread
// safety analysis is unable to prove that the accesses to |signals_|
// are always protected.
template <typename LockType>
void Dispatcher::UpdateStateHelper(zx_signals_t clear_mask,
                                   zx_signals_t set_mask,
                                   Lock<LockType>* lock) TA_NO_THREAD_SAFETY_ANALYSIS {
    Dispatcher::ObserverList obs_to_remove;

    {
        Guard<LockType> guard{lock};

        auto previous_signals = signals_;
        signals_ &= ~clear_mask;
        signals_ |= set_mask;

        if (previous_signals == signals_)
            return;

        UpdateInternalLocked(&obs_to_remove, signals_);
    }

    while (!obs_to_remove.is_empty()) {
        obs_to_remove.pop_front()->OnRemoved();
    }
}

void Dispatcher::UpdateState(zx_signals_t clear_mask,
                             zx_signals_t set_mask) {
    ZX_DEBUG_ASSERT(has_state_tracker());

    UpdateStateHelper(clear_mask, set_mask, get_lock());
}

void Dispatcher::UpdateStateLocked(zx_signals_t clear_mask,
                                   zx_signals_t set_mask) {
    ZX_DEBUG_ASSERT(has_state_tracker());

    // Type tag and local NullLock to make lockdep happy.
    struct DispatcherUpdateStateLocked {};
    DECLARE_LOCK(DispatcherUpdateStateLocked, fbl::NullLock) lock;
    UpdateStateHelper(clear_mask, set_mask, &lock);
}

zx_status_t Dispatcher::SetCookie(CookieJar* cookiejar, zx_koid_t scope, uint64_t cookie) {
    ZX_DEBUG_ASSERT(has_state_tracker());

    if (cookiejar == nullptr)
        return ZX_ERR_NOT_SUPPORTED;

    Guard<fbl::Mutex> guard{get_lock()};

    if (cookiejar->scope_ == ZX_KOID_INVALID) {
        cookiejar->scope_ = scope;
        cookiejar->cookie_ = cookie;

        kcounter_add(dispatcher_cookie_set_count, 1);
        return ZX_OK;
    }

    if (cookiejar->scope_ == scope) {
        cookiejar->cookie_ = cookie;

        kcounter_add(dispatcher_cookie_reset_count, 1);
        return ZX_OK;
    }

    return ZX_ERR_ACCESS_DENIED;
}

zx_status_t Dispatcher::GetCookie(CookieJar* cookiejar, zx_koid_t scope, uint64_t* cookie) {
    ZX_DEBUG_ASSERT(has_state_tracker());

    if (cookiejar == nullptr)
        return ZX_ERR_NOT_SUPPORTED;

    Guard<fbl::Mutex> guard{get_lock()};

    if (cookiejar->scope_ == scope) {
        *cookie = cookiejar->cookie_;
        return ZX_OK;
    }

    return ZX_ERR_ACCESS_DENIED;
}

zx_status_t Dispatcher::InvalidateCookieLocked(CookieJar* cookiejar) {
    ZX_DEBUG_ASSERT(has_state_tracker());

    if (cookiejar == nullptr)
        return ZX_ERR_NOT_SUPPORTED;

    cookiejar->scope_ = ZX_KOID_KERNEL;
    return ZX_OK;
}

zx_status_t Dispatcher::InvalidateCookie(CookieJar* cookiejar) {
    Guard<fbl::Mutex> guard{get_lock()};
    return InvalidateCookieLocked(cookiejar);
}

void Dispatcher::UpdateInternalLocked(ObserverList* obs_to_remove, zx_signals_t signals) {
    ZX_DEBUG_ASSERT(has_state_tracker());

    for (auto it = observers_.begin(); it != observers_.end();) {
        StateObserver::Flags it_flags = it->OnStateChange(signals);
        if (it_flags & StateObserver::kNeedRemoval) {
            auto to_remove = it;
            ++it;
            obs_to_remove->push_back(observers_.erase(to_remove));
        } else {
            ++it;
        }
    }
}
