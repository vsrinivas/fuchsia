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

void AsyncEventPair::SignalSelf(zx_signals_t to_clear, zx_signals_t to_set) const {
  auto status = eventpair_.signal(to_clear, to_set);
  FX_DCHECK(status == ZX_OK || status == ZX_ERR_BAD_HANDLE || status == ZX_ERR_PEER_CLOSED)
      << zx_status_get_string(status);
}

void AsyncEventPair::SignalPeer(zx_signals_t to_clear, zx_signals_t to_set) const {
  auto status = eventpair_.signal_peer(to_clear, to_set);
  FX_DCHECK(status == ZX_OK || status == ZX_ERR_BAD_HANDLE || status == ZX_ERR_PEER_CLOSED)
      << zx_status_get_string(status);
}

zx_signals_t AsyncEventPair::GetSignals(zx_signals_t signals) const {
  FX_DCHECK(eventpair_.is_valid());
  zx_signals_t observed = 0;
  eventpair_.wait_one(signals, zx::time::infinite_past(), &observed);
  return observed & signals;
}

ZxPromise<zx_signals_t> AsyncEventPair::WaitFor(zx_signals_t signals) {
  return executor_
      ->MakePromiseWaitHandle(zx::unowned_handle(eventpair_.get()),
                              signals | ZX_EVENTPAIR_PEER_CLOSED)
      .and_then([signals](const zx_packet_signal_t& packet) -> ZxResult<zx_signals_t> {
        if (packet.observed & ZX_EVENTPAIR_PEER_CLOSED) {
          return fpromise::error(ZX_ERR_PEER_CLOSED);
        }
        return fpromise::ok(packet.observed & signals);
      })
      .or_else([this](zx_status_t& status) {
        FX_DCHECK(status == ZX_ERR_CANCELED || status == ZX_ERR_BAD_HANDLE ||
                  status == ZX_ERR_PEER_CLOSED)
            << zx_status_get_string(status);
        eventpair_.reset();
        return fpromise::error(status);
      })
      .wrap_with(scope_);
}

}  // namespace fuzzing
