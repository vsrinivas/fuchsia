// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/async-eventpair.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/status.h>

namespace fuzzing {

AsyncEventPair::AsyncEventPair(ExecutorPtr executor) : executor_(std::move(executor)) {}

zx::eventpair AsyncEventPair::Create() {
  zx::eventpair eventpair;
  auto status = zx::eventpair::create(0, &eventpair_, &eventpair);
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
  return eventpair;
}

void AsyncEventPair::Pair(zx::eventpair&& eventpair) {
  FX_DCHECK(eventpair.is_valid());
  eventpair_ = std::move(eventpair);
}

bool AsyncEventPair::IsConnected() {
  return eventpair_.is_valid() && GetSignals(ZX_EVENTPAIR_PEER_CLOSED) == 0;
}

zx_status_t AsyncEventPair::SignalSelf(zx_signals_t to_clear, zx_signals_t to_set) {
  // Only modify user signals.
  to_clear &= ZX_USER_SIGNAL_ALL;
  to_set &= ZX_USER_SIGNAL_ALL;
  if (eventpair_.signal(to_clear, to_set) != ZX_OK) {
    eventpair_.reset();
    return ZX_ERR_PEER_CLOSED;
  }
  return ZX_OK;
}

zx_status_t AsyncEventPair::SignalPeer(zx_signals_t to_clear, zx_signals_t to_set) {
  // Only modify user signals.
  to_clear &= ZX_USER_SIGNAL_ALL;
  to_set &= ZX_USER_SIGNAL_ALL;
  if (eventpair_.signal_peer(to_clear, to_set) != ZX_OK) {
    eventpair_.reset();
    return ZX_ERR_PEER_CLOSED;
  }
  return ZX_OK;
}

zx_signals_t AsyncEventPair::GetSignals(zx_signals_t signals) {
  if (!eventpair_.is_valid()) {
    return 0;
  }
  zx_signals_t observed = 0;
  auto status = eventpair_.wait_one(signals, zx::time::infinite_past(), &observed);
  switch (status) {
    case ZX_OK:
      return observed & signals;
    case ZX_ERR_TIMED_OUT:
      return 0;
    default:
      eventpair_.reset();
      return 0;
  }
}

ZxPromise<zx_signals_t> AsyncEventPair::WaitFor(zx_signals_t signals) {
  return fpromise::make_promise([this, signals, wait = ZxFuture<zx_packet_signal_t>()](
                                    Context& context) mutable -> ZxResult<zx_packet_signal_t> {
           if (!eventpair_) {
             return fpromise::error(ZX_ERR_PEER_CLOSED);
           }
           if (!wait) {
             wait = executor_->MakePromiseWaitHandle(zx::unowned_handle(eventpair_.get()),
                                                     signals | ZX_EVENTPAIR_PEER_CLOSED);
           }
           if (!wait(context)) {
             suspended_ = context.suspend_task();
             return fpromise::pending();
           }
           return wait.take_result();
         })
      .and_then([signals](const zx_packet_signal_t& packet) -> ZxResult<zx_signals_t> {
        if (packet.observed & ZX_EVENTPAIR_PEER_CLOSED) {
          return fpromise::error(ZX_ERR_PEER_CLOSED);
        }
        return fpromise::ok(packet.observed & signals);
      })
      .or_else([this](zx_status_t& status) {
        eventpair_.reset();
        return fpromise::error(ZX_ERR_PEER_CLOSED);
      })
      .wrap_with(scope_);
}

void AsyncEventPair::Reset() {
  eventpair_.reset();
  suspended_.resume_task();
}

}  // namespace fuzzing
