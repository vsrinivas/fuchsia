// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/util/event_timestamper.h"

#include <lib/async/default.h>

namespace scenic {
namespace gfx {

EventTimestamper::EventTimestamper()
    : main_dispatcher_(async_get_default_dispatcher()),
      background_loop_(&kAsyncLoopConfigNoAttachToThread),
      task_([] { zx_thread_set_priority(24 /* HIGH_PRIORITY in LK */); }) {
  FXL_DCHECK(main_dispatcher_);
  background_loop_.StartThread();

  IncreaseBackgroundThreadPriority();
}

EventTimestamper::~EventTimestamper() {
  background_loop_.Shutdown();
#ifndef NDEBUG
  FXL_CHECK(watch_count_ == 0);
#endif
}

void EventTimestamper::IncreaseBackgroundThreadPriority() {
  task_.Post(background_loop_.dispatcher());
}

EventTimestamper::Watch::Watch() : waiter_(nullptr), timestamper_(nullptr) {}

EventTimestamper::Watch::Watch(EventTimestamper* ts, zx::event event,
                               zx_status_t trigger, Callback callback)
    : waiter_(new EventTimestamper::Waiter(ts->main_dispatcher_,
                                           std::move(event), trigger,
                                           std::move(callback))),
      timestamper_(ts) {
  FXL_DCHECK(timestamper_);
#ifndef NDEBUG
  ++timestamper_->watch_count_;
#endif
}

EventTimestamper::Watch::Watch(Watch&& rhs)
    : waiter_(rhs.waiter_), timestamper_(rhs.timestamper_) {
  rhs.waiter_ = nullptr;
  rhs.timestamper_ = nullptr;
}

EventTimestamper::Watch& EventTimestamper::Watch::operator=(
    EventTimestamper::Watch&& rhs) {
  FXL_DCHECK(!waiter_ && !timestamper_);
  waiter_ = rhs.waiter_;
  timestamper_ = rhs.timestamper_;
  rhs.waiter_ = nullptr;
  rhs.timestamper_ = nullptr;
  return *this;
}

EventTimestamper::Watch::~Watch() {
  if (!waiter_) {
    // Was moved.
    return;
  }
#ifndef NDEBUG
  --timestamper_->watch_count_;
#endif

  switch (waiter_->state()) {
    case Waiter::State::STOPPED:
      delete waiter_;
      break;
    case Waiter::State::STARTED:
      waiter_->set_state(Waiter::State::ABANDONED);
      if (ZX_OK == waiter_->wait().Cancel()) {
        // We successfully cancelled the async::Wait, so we can/must delete the
        // Waiter ourselves.  Otherwise we must not delete it, because it will
        // delete itself when it sees that it has been abandoned.
        delete waiter_;
      }
      break;
    case Waiter::State::ABANDONED:
      FXL_DCHECK(false) << "internal error.";
      break;
  }
}

void EventTimestamper::Watch::Start() {
  FXL_DCHECK(waiter_) << "invalid Watch (was it std::move()d?).";
  FXL_DCHECK(waiter_->state() == Waiter::State::STOPPED)
      << "illegal to call Start() again before callback has been received.";
  waiter_->set_state(Waiter::State::STARTED);
  waiter_->wait().Begin(timestamper_->background_loop_.dispatcher());
}

// Return the watched event (or a null handle, if this Watch was moved).
const zx::event& EventTimestamper::Watch::event() const {
  static const zx::event null_handle;
  return waiter_ ? waiter_->event() : null_handle;
}

EventTimestamper::Waiter::Waiter(async_dispatcher_t* dispatcher, zx::event event,
                                 zx_status_t trigger, Callback callback)
    : dispatcher_(dispatcher),
      event_(std::move(event)),
      callback_(std::move(callback)),
      wait_(this, event_.get(), trigger) {}

EventTimestamper::Waiter::~Waiter() {
  FXL_DCHECK(state_ == State::STOPPED || state_ == State::ABANDONED);
}

void EventTimestamper::Waiter::Handle(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                      zx_status_t status,
                                      const zx_packet_signal_t* signal) {
  zx_time_t now = zx_clock_get(ZX_CLOCK_MONOTONIC);
  async::PostTask(dispatcher_, [now, this] {
    if (state_ == State::ABANDONED) {
      // The EventTimestamper::Watch that owned us was destroyed; we must
      // immediately destroy ourself or our memory will be leaked.
      delete this;
      return;
    }
    FXL_DCHECK(state_ == State::STARTED) << "internal error.";
    state_ = State::STOPPED;
    callback_(now);
  });
}

}  // namespace gfx
}  // namespace scenic
