// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_TESTING_SIGNAL_COORDINATOR_H_
#define SRC_SYS_FUZZING_COMMON_TESTING_SIGNAL_COORDINATOR_H_

#include <mutex>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/sys/fuzzing/common/signal-coordinator.h"
#include "src/sys/fuzzing/common/sync-wait.h"

namespace fuzzing {

// Wraps a |SignalCoordinator| and automatically provides a simple signal handler that can be used
// to wait synchronously for a signal.
class FakeSignalCoordinator final {
 public:
  FakeSignalCoordinator() = default;
  ~FakeSignalCoordinator() = default;

  bool is_valid() const { return coordinator_.is_valid(); }

  // Like |SignalCoordinator::Create| with |OnSignal| as the second parameter.
  zx::eventpair Create();

  // Like |SignalCoordinator::Pair| with |OnSignal| as the second parameter.
  void Pair(zx::eventpair paired);

  // Fakes sending a signal to the peer.
  bool SignalPeer(Signal signal) FXL_LOCKS_EXCLUDED(mutex_);

  // Blocks until the next call to |SignalPeer|.
  zx_signals_t AwaitSignal() FXL_LOCKS_EXCLUDED(mutex_);

  // Like |AwaitSignal()| but only blocks until |deadline| and returns signals via |out|.
  zx_status_t AwaitSignal(zx::time deadline, zx_signals_t* out) FXL_LOCKS_EXCLUDED(mutex_);

  void Join() { coordinator_.Join(); }
  void Reset() { coordinator_.Reset(); }

 private:
  bool OnSignal(zx_signals_t observed);
  zx_signals_t GetObserved() FXL_LOCKS_EXCLUDED(mutex_);

  SignalCoordinator coordinator_;
  std::mutex mutex_;
  SyncWait sync_;
  zx_signals_t observed_ FXL_GUARDED_BY(mutex_) = 0;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(FakeSignalCoordinator);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_TESTING_SIGNAL_COORDINATOR_H_
