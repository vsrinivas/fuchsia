// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/dispatcher.h"

#include <inttypes.h>
#include <lib/counters.h>
#include <lib/ktrace.h>

#include <arch/ops.h>
#include <kernel/mutex.h>
#include <ktl/atomic.h>

// kernel counters. The following counters never decrease.
// counts the number of times a dispatcher has been created and destroyed.
KCOUNTER(dispatcher_create_count, "dispatcher.create")
KCOUNTER(dispatcher_destroy_count, "dispatcher.destroy")
// counts the number of times observers have been added to a kernel object.
KCOUNTER(dispatcher_observe_count, "dispatcher.observer.add")
// counts the number of times observers have been canceled.
KCOUNTER(dispatcher_cancel_count, "dispatcher.observer.cancel")
KCOUNTER(dispatcher_cancel_bk_count, "dispatcher.observer.cancel.by_key.handled")
KCOUNTER(dispatcher_cancel_bk_nh_count, "dispatcher.observer.cancel.by_key.not_handled")

namespace {
ktl::atomic<zx_koid_t> global_koid(ZX_KOID_FIRST);

zx_koid_t GenerateKernelObjectId() {
  return global_koid.fetch_add(1ULL, ktl::memory_order_relaxed);
}

// Helper class that safely allows deleting Dispatchers without risk of
// blowing up the kernel stack.  It uses one pointer in the Thread
// structure to unwind the recursion.
class SafeDeleter {
 public:
  static void Delete(Dispatcher* kobj) {
    auto recursive_deleter =
        static_cast<SafeDeleter*>(Thread::Current::Get()->recursive_object_deletion_list());
    if (recursive_deleter) {
      // Delete() was called recursively.
      recursive_deleter->pending_.push_front(kobj);
    } else {
      SafeDeleter deleter;
      Thread::Current::Get()->set_recursive_object_deletion_list(&deleter);

      do {
        // This delete call can cause recursive calls to
        // Dispatcher::fbl_recycle() and hence to Delete().
        delete kobj;

        kobj = deleter.pending_.pop_front();
      } while (kobj);

      Thread::Current::Get()->set_recursive_object_deletion_list(nullptr);
    }
  }

 private:
  fbl::SinglyLinkedListCustomTraits<Dispatcher*, Dispatcher::DeleterListTraits> pending_;
};

}  // namespace

Dispatcher::Dispatcher(zx_signals_t signals)
    : koid_(GenerateKernelObjectId()), handle_count_(0u), signals_(signals) {
  kcounter_add(dispatcher_create_count, 1);
}

Dispatcher::~Dispatcher() {
  ktrace(TAG_OBJECT_DELETE, (uint32_t)koid_, 0, 0, 0);
  kcounter_add(dispatcher_destroy_count, 1);
}

// The refcount of this object has reached zero: delete self
// using the SafeDeleter to avoid potential recursion hazards.
// TODO(cpu): Not all object need the SafeDeleter. Only objects
// that can control the lifetime of dispatchers that in turn
// can control the lifetime of others. For example events do
// not fall in this category.
void Dispatcher::fbl_recycle() {
  canary_.Assert();

  SafeDeleter::Delete(this);
}

template <typename Func>
StateObserver::Flags Dispatcher::CancelWithFunc(Func f) {
  StateObserver::Flags flags = 0;

  for (auto it = observers_.begin(); it != observers_.end();) {
    StateObserver::Flags it_flags = f(it.CopyPointer());
    flags |= it_flags;
    if (it_flags & StateObserver::kNeedRemoval) {
      auto to_remove = it;
      ++it;
      observers_.erase(to_remove);
      to_remove->OnRemoved();
      kcounter_add(dispatcher_cancel_count, 1);
    } else {
      ++it;
    }
  }

  // We've processed the removal flag, so strip it
  return flags & (~StateObserver::kNeedRemoval);
}

zx_status_t Dispatcher::AddObserver(StateObserver* observer) {
  DEBUG_ASSERT(observer != nullptr);

  canary_.Assert();

  if (!is_waitable()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  {
    Guard<Mutex> guard{get_lock()};

    StateObserver::Flags flags = observer->OnInitialize(signals_);
    if (flags & StateObserver::kNeedRemoval) {
      observer->OnRemoved();
    } else {
      observers_.push_front(observer);
    }
  }

  kcounter_add(dispatcher_observe_count, 1);
  return ZX_OK;
}

bool Dispatcher::RemoveObserver(StateObserver* observer) {
  canary_.Assert();
  ZX_DEBUG_ASSERT(is_waitable());
  DEBUG_ASSERT(observer != nullptr);

  Guard<Mutex> guard{get_lock()};

  if (StateObserver::ObserverListTraits::node_state(*observer).InContainer()) {
    observers_.erase(*observer);
    return true;
  }

  return false;
}

zx_status_t Dispatcher::AddObserver(SignalObserver* observer, const Handle* handle,
                                    zx_signals_t signals) {
  canary_.Assert();
  ZX_DEBUG_ASSERT(observer != nullptr);

  if (!is_waitable()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  kcounter_add(dispatcher_observe_count, 1);

  Guard<Mutex> guard{get_lock()};

  // If the currently active signals already match the desired signals,
  // just execute the match now.
  if ((signals_ & signals) != 0) {
    observer->OnMatch(signals_);
    return ZX_OK;
  }

  // Otherwise, enqueue this observer.
  observer->handle_ = handle;
  observer->triggering_signals_ = signals;
  signal_observers_.push_front(observer);

  return ZX_OK;
}

bool Dispatcher::RemoveObserver(SignalObserver* observer, zx_signals_t* signals) {
  canary_.Assert();

  ZX_DEBUG_ASSERT(is_waitable());
  ZX_DEBUG_ASSERT(observer != nullptr);

  Guard<Mutex> guard{get_lock()};

  if (signals != nullptr) {
    *signals = signals_;
  }

  if (observer->InContainer()) {
    signal_observers_.erase(*observer);
    return true;
  }

  return false;
}

void Dispatcher::Cancel(const Handle* handle) {
  canary_.Assert();
  ZX_DEBUG_ASSERT(is_waitable());
  int32_t cancel_count = 0;

  {
    Guard<Mutex> guard{get_lock()};

    // Cancel StateObservers.
    CancelWithFunc([handle](StateObserver* obs) { return obs->OnCancel(handle); });

    // Cancel all observers that registered on "handle".
    for (auto it = signal_observers_.begin(); it != signal_observers_.end(); /* nothing */) {
      if (it->handle_ != handle) {
        ++it;
        continue;
      }

      // Remove the element.
      auto to_remove = it;
      ++it;
      signal_observers_.erase(to_remove);
      to_remove->OnCancel(signals_);
      cancel_count++;
    }
  }

  kcounter_add(dispatcher_cancel_count, cancel_count);
}

bool Dispatcher::CancelByKey(const Handle* handle, const void* port, uint64_t key) {
  canary_.Assert();
  ZX_DEBUG_ASSERT(is_waitable());

  bool remove_performed = false;
  uint32_t cancel_count = 0;

  {
    Guard<Mutex> guard{get_lock()};

    StateObserver::Flags flags = CancelWithFunc(
        [handle, port, key](StateObserver* obs) { return obs->OnCancelByKey(handle, port, key); });
    if (flags & StateObserver::kHandled) {
      remove_performed = true;
    }

    // Cancel all observers that registered on "handle" that match the given key.
    for (auto it = signal_observers_.begin(); it != signal_observers_.end(); /* nothing */) {
      if (it->handle_ != handle || !it->MatchesKey(port, key)) {
        ++it;
        continue;
      }

      // Remove the element.
      auto to_remove = it;
      ++it;
      signal_observers_.erase(to_remove);
      to_remove->OnCancel(signals_);
      remove_performed = true;
      cancel_count++;
    }
  }

  kcounter_add(dispatcher_cancel_count, cancel_count);

  if (remove_performed) {
    kcounter_add(dispatcher_cancel_bk_count, 1);
    return true;
  }

  kcounter_add(dispatcher_cancel_bk_nh_count, 1);
  return false;
}

void Dispatcher::UpdateState(zx_signals_t clear_mask, zx_signals_t set_mask) {
  canary_.Assert();

  Guard<Mutex> guard{get_lock()};

  UpdateStateLocked(clear_mask, set_mask);
}

void Dispatcher::UpdateStateLocked(zx_signals_t clear_mask, zx_signals_t set_mask) {
  ZX_DEBUG_ASSERT(is_waitable());

  auto previous_signals = signals_;
  signals_ &= ~clear_mask;
  signals_ |= set_mask;

  if (previous_signals == signals_) {
    return;
  }

  // Update state observers.
  for (auto it = observers_.begin(); it != observers_.end(); /* nothing */) {
    StateObserver::Flags it_flags = it->OnStateChange(signals_);
    if (it_flags & StateObserver::kNeedRemoval) {
      auto to_remove = it;
      ++it;
      observers_.erase(to_remove);
      to_remove->OnRemoved();
    } else {
      ++it;
    }
  }

  // Update signal observers.
  for (auto it = signal_observers_.begin(); it != signal_observers_.end(); /* nothing */) {
    // Ignore observers that don't need to be notified.
    if ((it->triggering_signals_ & signals_) == 0) {
      ++it;
      continue;
    }

    auto to_remove = it;
    ++it;
    signal_observers_.erase(to_remove);
    to_remove->OnMatch(signals_);
  }
}

zx_signals_t Dispatcher::PollSignals() const {
  Guard<Mutex> guard{get_lock()};
  return signals_;
}
