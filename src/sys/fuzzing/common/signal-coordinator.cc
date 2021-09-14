// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/signal-coordinator.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/status.h>

namespace fuzzing {

SignalCoordinator::~SignalCoordinator() { Reset(); }

bool SignalCoordinator::is_valid() const {
  return paired_.is_valid() && paired_.wait_one(ZX_EVENTPAIR_PEER_CLOSED, zx::time::infinite_past(),
                                                nullptr) == ZX_ERR_TIMED_OUT;
}

zx::eventpair SignalCoordinator::Create(SignalHandler on_signal) {
  Reset();
  zx::eventpair paired;
  zx_status_t status = zx::eventpair::create(0, &paired_, &paired);
  if (status != ZX_OK) {
    FX_LOGS(FATAL) << "Failed to create eventpair: " << zx_status_get_string(status);
  }
  on_signal_ = std::move(on_signal);
  WaitLoop();
  return paired;
}

void SignalCoordinator::Pair(zx::eventpair paired, SignalHandler on_signal) {
  Reset();
  if (!paired.is_valid()) {
    FX_LOGS(FATAL) << "Received bad eventpair.";
  }
  paired_ = std::move(paired);
  on_signal_ = std::move(on_signal);
  WaitLoop();
}

void SignalCoordinator::WaitLoop() {
  wait_loop_ = std::thread([this]() {
    while (true) {
      zx_signals_t observed;
      auto status = paired_.wait_one(ZX_USER_SIGNAL_ALL | ZX_EVENTPAIR_PEER_CLOSED,
                                     zx::time::infinite(), &observed);
      // Check if another thread reset |paired_| before the call to |wait_one|, or during it.
      if (status == ZX_ERR_BAD_HANDLE || status == ZX_ERR_CANCELED) {
        break;
      }
      if (status != ZX_OK) {
        FX_LOGS(FATAL) << "Failed to wait for eventpair peer: " << zx_status_get_string(status);
      }
      // Check if the other end reset the connection.
      if (observed & ZX_EVENTPAIR_PEER_CLOSED) {
        break;
      }
      // Check if another thread reset |paired_| before the call to |signal|.
      status = paired_.signal(observed, 0);
      if (status == ZX_ERR_BAD_HANDLE) {
        break;
      }
      if (status != ZX_OK) {
        FX_LOGS(FATAL) << "Failed to clear eventpair: " << zx_status_get_string(status);
      }
      if (!on_signal_(observed)) {
        break;
      }
    }
    paired_.reset();
    on_signal_(ZX_EVENTPAIR_PEER_CLOSED);
  });
}

bool SignalCoordinator::SignalPeer(Signal signal) {
  auto status = paired_.signal_peer(0, signal);
  // Check if another thread reset |paired_| or if the other end reset the connection.
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
