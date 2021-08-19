// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/testing/signal-coordinator.h"

namespace fuzzing {

zx::eventpair FakeSignalCoordinator::Create() {
  return coordinator_.Create([this](zx_signals_t observed) { return OnSignal(observed); });
}

void FakeSignalCoordinator::Pair(zx::eventpair paired) {
  coordinator_.Pair(std::move(paired),
                    [this](zx_signals_t observed) { return OnSignal(observed); });
}

bool FakeSignalCoordinator::SignalPeer(Signal signal) { return coordinator_.SignalPeer(signal); }

zx_signals_t FakeSignalCoordinator::AwaitSignal() {
  sync_completion_wait(&sync_, ZX_TIME_INFINITE);
  sync_completion_reset(&sync_);
  return observed_;
}

bool FakeSignalCoordinator::OnSignal(zx_signals_t observed) {
  observed_ = observed;
  sync_completion_signal(&sync_);
  return (observed & ZX_EVENTPAIR_PEER_CLOSED) == 0;
}

}  // namespace fuzzing
