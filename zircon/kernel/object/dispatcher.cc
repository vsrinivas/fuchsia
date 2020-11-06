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

// Counts the number of times a dispatcher has been created and destroyed.
KCOUNTER(dispatcher_create_count, "dispatcher.create")
KCOUNTER(dispatcher_destroy_count, "dispatcher.destroy")

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

zx_status_t Dispatcher::AddObserver(SignalObserver* observer, const Handle* handle,
                                    zx_signals_t signals, Dispatcher::TriggerMode trigger_mode) {
  canary_.Assert();
  ZX_DEBUG_ASSERT(observer != nullptr);

  if (!is_waitable()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  Guard<Mutex> guard{get_lock()};

  if (trigger_mode == Dispatcher::TriggerMode::Level) {
    // If the currently active signals already match the desired signals,
    // just execute the match now.
    if ((signals_ & signals) != 0) {
      observer->OnMatch(signals_);
      return ZX_OK;
    }
  }

  // Otherwise, enqueue this observer.
  observer->handle_ = handle;
  observer->triggering_signals_ = signals;
  observers_.push_front(observer);

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
    observers_.erase(*observer);
    return true;
  }

  return false;
}

void Dispatcher::Cancel(const Handle* handle) {
  canary_.Assert();
  ZX_DEBUG_ASSERT(is_waitable());

  Guard<Mutex> guard{get_lock()};

  // Cancel all observers that registered on "handle".
  for (auto it = observers_.begin(); it != observers_.end(); /* nothing */) {
    if (it->handle_ != handle) {
      ++it;
      continue;
    }

    // Remove the element.
    auto to_remove = it;
    ++it;
    observers_.erase(to_remove);
    to_remove->OnCancel(signals_);
  }
}

bool Dispatcher::CancelByKey(const Handle* handle, const void* port, uint64_t key) {
  canary_.Assert();
  ZX_DEBUG_ASSERT(is_waitable());

  Guard<Mutex> guard{get_lock()};

  // Cancel all observers that registered on "handle" that match the given key.
  bool remove_performed = false;
  for (auto it = observers_.begin(); it != observers_.end(); /* nothing */) {
    if (it->handle_ != handle || !it->MatchesKey(port, key)) {
      ++it;
      continue;
    }

    // Remove the element.
    auto to_remove = it;
    ++it;
    observers_.erase(to_remove);
    to_remove->OnCancel(signals_);
    remove_performed = true;
  }

  return remove_performed;
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

  // Update signal observers.
  for (auto it = observers_.begin(); it != observers_.end(); /* nothing */) {
    // Ignore observers that don't need to be notified.
    if ((it->triggering_signals_ & signals_) == 0) {
      ++it;
      continue;
    }

    auto to_remove = it;
    ++it;
    observers_.erase(to_remove);
    to_remove->OnMatch(signals_);
  }
}

zx_signals_t Dispatcher::PollSignals() const {
  Guard<Mutex> guard{get_lock()};
  return signals_;
}
