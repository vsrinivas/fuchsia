// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_WAIT_SIGNAL_OBSERVER_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_WAIT_SIGNAL_OBSERVER_H_

#include <stdint.h>
#include <zircon/types.h>

#include <fbl/canary.h>
#include <fbl/ref_ptr.h>
#include <kernel/event.h>
#include <ktl/atomic.h>
#include <object/dispatcher.h>
#include <object/signal_observer.h>

class Event;

// Helper class for Waiting on the wait_one and wait_many syscalls.
class WaitSignalObserver final : public SignalObserver {
 public:
  WaitSignalObserver() : SignalObserver() {}
  ~WaitSignalObserver() final;

  // This should be called under the handle table lock. If this succeeds, End() must be called
  // (before the Event is destroyed).
  zx_status_t Begin(Event* event, Handle* handle, zx_signals_t watched_signals);

  // This should *not* be called under the handle table lock.
  zx_signals_t End();

 private:
  WaitSignalObserver(const WaitSignalObserver&) = delete;
  WaitSignalObserver& operator=(const WaitSignalObserver&) = delete;

  // |SignalObserver| implementation.
  void OnMatch(zx_signals_t signals) final;
  void OnCancel(zx_signals_t signals) final;

  fbl::Canary<fbl::magic("WTSO")> canary_;

  Event* event_ = nullptr;
  fbl::RefPtr<Dispatcher> dispatcher_;  // Non-null only between Begin() and End().

  // Snapshot of the watched object's signals. This is written to
  // precisely once if either |OnMatch| or |OnCancel| is called. We
  // inspect it once after |this| is no longer watching the observer.
  ktl::atomic<zx_signals_t> final_signal_state_ = 0;
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_WAIT_SIGNAL_OBSERVER_H_
