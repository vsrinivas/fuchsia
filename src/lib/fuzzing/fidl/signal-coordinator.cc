// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "signal-coordinator.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/status.h>

namespace fuzzing {

SignalCoordinator::~SignalCoordinator() { Reset(); }

void SignalCoordinator::CreateImpl(zx::eventpair* out) {
  Reset();
  zx_status_t status = zx::eventpair::create(0, &paired_, out);
  if (status != ZX_OK) {
    FX_LOGS(FATAL) << "Failed to create eventpair: " << zx_status_get_string(status);
  }
}

void SignalCoordinator::PairImpl(zx::eventpair paired) {
  Reset();
  if (!paired.is_valid()) {
    FX_LOGS(FATAL) << "Received bad eventpair.";
  }
  paired_ = std::move(paired);
}

zx_signals_t SignalCoordinator::WaitOne() {
  zx_signals_t observed;
  zx_status_t status = paired_.wait_one(ZX_USER_SIGNAL_ALL | ZX_EVENTPAIR_PEER_CLOSED,
                                        zx::time::infinite(), &observed);
  // Check if another thread reset |paired_| before the call to |wait_one|, or during it.
  if (status == ZX_ERR_BAD_HANDLE || status == ZX_ERR_CANCELED) {
    return ZX_EVENTPAIR_PEER_CLOSED;
  }
  if (status != ZX_OK) {
    FX_LOGS(FATAL) << "Failed to wait for eventpair peer: " << zx_status_get_string(status);
  }
  // Check if the other end reset the connection.
  if (observed & ZX_EVENTPAIR_PEER_CLOSED) {
    return ZX_EVENTPAIR_PEER_CLOSED;
  }
  // Check if another thread reset |paired_| before the call to |signal|.
  status = paired_.signal(observed, 0);
  if (status == ZX_ERR_BAD_HANDLE) {
    return ZX_EVENTPAIR_PEER_CLOSED;
  }
  if (status != ZX_OK) {
    FX_LOGS(FATAL) << "Failed to clear eventpair: " << zx_status_get_string(status);
  }
  return observed;
}

bool SignalCoordinator::SignalPeer(Signal signal) {
  // Check if another thread reset |paired_| before the call to |signal_peer|, or if the other end
  // reset the connection.
  zx_status_t status = paired_.signal_peer(0, signal);
  if (status == ZX_ERR_BAD_HANDLE || status == ZX_ERR_PEER_CLOSED) {
    return false;
  }
  if (status != ZX_OK) {
    FX_LOGS(FATAL) << "Failed to signal eventpair peer: " << zx_status_get_string(status);
  }
  return true;
}

void SignalCoordinator::Reset() {
  paired_.reset();
  Join();
}

void SignalCoordinator::Join() {
  if (wait_loop_.joinable()) {
    wait_loop_.join();
  }
}

}  // namespace fuzzing
