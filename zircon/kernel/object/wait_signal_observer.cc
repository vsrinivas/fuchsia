// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/wait_signal_observer.h"

#include <assert.h>

#include <kernel/event.h>
#include <ktl/atomic.h>
#include <object/dispatcher.h>
#include <object/handle.h>

#include <ktl/enforce.h>

WaitSignalObserver::~WaitSignalObserver() { DEBUG_ASSERT(!dispatcher_); }

zx_status_t WaitSignalObserver::Begin(Event* event, Handle* handle, zx_signals_t watched_signals) {
  canary_.Assert();
  DEBUG_ASSERT(!dispatcher_);

  event_ = event;
  dispatcher_ = handle->dispatcher();

  // Wait for one of |watched_signals| to become active.
  //
  // Note that |watched_signals| may be 0, in which case we won't receive
  // a callback, but will remain queued until |End| is called.
  zx_status_t status = handle->dispatcher()->AddObserver(this, handle, watched_signals);
  if (status != ZX_OK) {
    dispatcher_.reset();
    return status;
  }

  return ZX_OK;
}

zx_signals_t WaitSignalObserver::End() {
  canary_.Assert();
  DEBUG_ASSERT(dispatcher_);

  // Otherwise, remove this observer from the dispatcher's observer list.
  zx_signals_t signals;
  bool was_removed = dispatcher_->RemoveObserver(this, &signals);
  dispatcher_.reset();

  // If |was_removed| is false, it means a callback was fired and |final_signal_state| has
  // a value.
  if (!was_removed) {
    return final_signal_state_.load(ktl::memory_order_acquire);
  }

  // Otherwise, return the set of signals at the point of removal.
  return signals;
}

void WaitSignalObserver::OnMatch(zx_signals_t signals) {
  canary_.Assert();

  // Save the signal state, and wake our waiter.
  final_signal_state_.store(signals, ktl::memory_order_release);
  event_->Signal();
}

void WaitSignalObserver::OnCancel(zx_signals_t signals) {
  canary_.Assert();

  // Save the signal state, and wake our waiter.
  final_signal_state_.store(signals | ZX_SIGNAL_HANDLE_CLOSED, ktl::memory_order_release);
  event_->Signal(ZX_ERR_CANCELED);
}
