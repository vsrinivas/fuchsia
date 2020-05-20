// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/wait_state_observer.h"

#include <assert.h>

#include <kernel/event.h>
#include <object/dispatcher.h>
#include <object/handle.h>

WaitStateObserver::~WaitStateObserver() { DEBUG_ASSERT(!dispatcher_); }

zx_status_t WaitStateObserver::Begin(Event* event, Handle* handle, zx_signals_t watched_signals) {
  canary_.Assert();
  DEBUG_ASSERT(!dispatcher_);

  event_ = event;
  handle_ = handle;
  watched_signals_ = watched_signals;
  dispatcher_ = handle->dispatcher();
  target_signal_state_.store(0u, ktl::memory_order_relaxed);

  auto status = dispatcher_->AddObserver(this);
  if (status != ZX_OK) {
    dispatcher_.reset();
    return status;
  }
  return ZX_OK;
}

zx_signals_t WaitStateObserver::End() {
  canary_.Assert();
  DEBUG_ASSERT(dispatcher_);

  // Remove this observer from the dispatcher if it hasn't already been removed.
  dispatcher_->RemoveObserver(this);
  dispatcher_.reset();

  // Return the set of signals that caused us to wake.
  return target_signal_state_.load(ktl::memory_order_acquire);
}

StateObserver::Flags WaitStateObserver::OnInitialize(zx_signals_t initial_state) {
  canary_.Assert();

  // Save the initial state of signals.
  target_signal_state_.store(initial_state, ktl::memory_order_release);

  // If the initial state of the object already has the expected signal,
  // we can wake up immediately.
  if ((initial_state & watched_signals_) != 0) {
    event_->Signal();
    return kNeedRemoval;
  }

  return 0;
}

StateObserver::Flags WaitStateObserver::OnStateChange(zx_signals_t new_state) {
  canary_.Assert();

  // Save a snapshot of our target object's signals.
  target_signal_state_.store(new_state, ktl::memory_order_release);

  // Signal the event if this is a watched signal.
  if ((new_state & watched_signals_) != 0) {
    event_->Signal();
    return kNeedRemoval;
  }

  return 0;
}

StateObserver::Flags WaitStateObserver::OnCancel(const Handle* handle) {
  canary_.Assert();

  if (handle != handle_) {
    return 0;
  }

  // Update the most recently seen set of signals, and also set the ZX_SIGNAL_HANDLE_CLOSED bit.
  zx_signals_t current_signals = target_signal_state_.load(ktl::memory_order_acquire);
  target_signal_state_.store(current_signals | ZX_SIGNAL_HANDLE_CLOSED, ktl::memory_order_release);
  event_->Signal(ZX_ERR_CANCELED);
  return kHandled | kNeedRemoval;
}
