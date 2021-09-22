// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_TESTING_SIGNAL_COORDINATOR_H_
#define SRC_SYS_FUZZING_COMMON_TESTING_SIGNAL_COORDINATOR_H_

#include <lib/sync/completion.h>

#include "src/sys/fuzzing/common/signal-coordinator.h"

namespace fuzzing {

// Wraps a |SignalCoordinator| and automatically provides a simple signal handler that can be used
// to wait synchronously for a signal.
class FakeSignalCoordinator final {
 public:
  FakeSignalCoordinator() = default;
  ~FakeSignalCoordinator() = default;

  // Like |SignalCoordinator::Create| with |OnSignal| as the second parameter.
  zx::eventpair Create();

  // Like |SignalCoordinator::Pair| with |OnSignal| as the second parameter.
  void Pair(zx::eventpair paired);

  // Fakes sending a signal to the peer.
  bool SignalPeer(Signal signal);

  // Blocks until the next call to |SignalPeer|.
  zx_signals_t AwaitSignal();

  void Join() { coordinator_.Join(); }
  void Reset() { coordinator_.Reset(); }

 private:
  bool OnSignal(zx_signals_t observed);

  SignalCoordinator coordinator_;
  sync_completion_t sync_;
  zx_signals_t observed_ = 0;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(FakeSignalCoordinator);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_TESTING_SIGNAL_COORDINATOR_H_
