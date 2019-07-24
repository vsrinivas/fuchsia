// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_WAIT_STATE_OBSERVER_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_WAIT_STATE_OBSERVER_H_

#include <stdint.h>
#include <zircon/types.h>

#include <fbl/canary.h>
#include <fbl/ref_ptr.h>
#include <kernel/event.h>
#include <ktl/atomic.h>
#include <object/dispatcher.h>
#include <object/state_observer.h>

class Event;

// Helper class for Waiting on the wait_one and wait_many syscalls.
class WaitStateObserver final : public StateObserver {
 public:
  WaitStateObserver() : StateObserver() {}
  ~WaitStateObserver();

  // This should be called under the handle table lock. If this succeeds, End() must be called
  // (before the Event is destroyed).
  zx_status_t Begin(Event* event, Handle* handle, zx_signals_t watched_signals);

  // This should *not* be called under the handle table lock.
  zx_signals_t End();

 private:
  WaitStateObserver(const WaitStateObserver&) = delete;
  WaitStateObserver& operator=(const WaitStateObserver&) = delete;

  // StateObserver implementation:
  Flags OnInitialize(zx_signals_t initial_state, const StateObserver::CountInfo* cinfo) final;
  Flags OnStateChange(zx_signals_t new_state) final;
  Flags OnCancel(const Handle* handle) final;

  fbl::Canary<fbl::magic("WTSO")> canary_;

  // handle_ only used as a cookie and no methods should be called on it.
  const void* handle_ = nullptr;
  Event* event_ = nullptr;
  zx_signals_t watched_signals_ = 0u;
  ktl::atomic<zx_signals_t> wakeup_reasons_;
  fbl::RefPtr<Dispatcher> dispatcher_;  // Non-null only between Begin() and End().
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_WAIT_STATE_OBSERVER_H_
