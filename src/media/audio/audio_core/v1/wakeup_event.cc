// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/wakeup_event.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/types.h>

namespace media::audio {
namespace {

constexpr zx_signals_t kWakeupEventSignal = ZX_USER_SIGNAL_0;

zx_status_t AssertWakeupEventSignal(const zx::event& event) {
  return event.signal(0, kWakeupEventSignal);
}

zx_status_t DeassertWakeupEventSignal(const zx::event& event) {
  return event.signal(kWakeupEventSignal, 0);
}

}  // namespace

WakeupEvent::WakeupEvent() {
  FX_CHECK(zx::event::create(0, &event_) == ZX_OK);
  wait_.set_object(event_.get());
  wait_.set_trigger(kWakeupEventSignal);
}

zx_status_t WakeupEvent::Activate(async_dispatcher_t* dispatcher, ProcessHandler process_handler) {
  process_handler_ = std::move(process_handler);
  return wait_.Begin(dispatcher);
}

zx_status_t WakeupEvent::Deactivate() {
  process_handler_ = nullptr;
  return wait_.Cancel();
}

zx_status_t WakeupEvent::Signal() const { return AssertWakeupEventSignal(event_); }

void WakeupEvent::OnSignals(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                            zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    // Cancellation is normal behavior
    if (status != ZX_ERR_CANCELED) {
      FX_PLOGS(ERROR, status) << "Async wait failed";
    }
    return;
  }

  if (signal->observed & kWakeupEventSignal) {
    // Deassert first so that the process handler can reassert if necessary.
    status = DeassertWakeupEventSignal(event_);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to clear signals";
      return;
    }

    FX_DCHECK(process_handler_);
    zx_status_t status = process_handler_(this);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Process handler failed";
      Deactivate();
      return;
    }

    status = wait->Begin(dispatcher);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to wait on signals";
      return;
    }
  }
}

}  // namespace media::audio
