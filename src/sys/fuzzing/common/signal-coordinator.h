// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_SIGNAL_COORDINATOR_H_
#define SRC_SYS_FUZZING_COMMON_SIGNAL_COORDINATOR_H_

#include <lib/zx/eventpair.h>
#include <zircon/types.h>

#include <thread>

#include "src/lib/fxl/macros.h"

namespace fuzzing {

// This enum renames some Zircon user signals to associate them with certain actions performed by
// the libFuzzer engine.
enum Signal : zx_signals_t {
  // Corresponds to the start of a fuzzing iteration, as in |fuzzer::Fuzzer::ExecuteCallback|.
  kExecuteCallback = ZX_USER_SIGNAL_0,

  // Corresponds to the end of a fuzzing iteration, similar to the call to libFuzzer's
  // |fuzzer::TracePC::CollectFeatures| in |fuzzer::Fuzzer::RunOne|.
  kCollectCoverage = ZX_USER_SIGNAL_1,

  // Instructs the remote process to perform an iteration checking for leaks.
  kTryDetectingALeak = ZX_USER_SIGNAL_0 | ZX_USER_SIGNAL_2,

  // Suggests to the fuzzer engine that a leak is likely in the previous iteration.
  kLeakDetected = ZX_USER_SIGNAL_1 | ZX_USER_SIGNAL_2,

  // Indicates the fuzzer is shutting done and end-of-process leak detection should be performed.
  kDetectLeaksAtExit = ZX_USER_SIGNAL_2,
};

// This class wraps an eventpair and thread to present a simple way for one process to signal
// another, and have that process respond. This class is used in this library with
// |fuzzing::Signals|.
class SignalCoordinator final {
 public:
  SignalCoordinator() = default;
  ~SignalCoordinator();

  // Both |Create| and |Pair| take an |on_signal| parameter, which should be callable with the
  // signature: bool on_signal(zx_signals_t signals);
  //
  // This will be called when a the other end of the event pair sends a Zircon user signal to this
  // end. If this method returns false, the wait loop will exit. When the wait loop exits for any
  // reason, this method will be called one final time with |ZX_EVENTPAIR_PEER_CLOSED|.

  // Creates an event pair and returns one end via |out|. If this object was previously created or
  // linked, it is first reset. See the above note on |on_signal|.
  template <typename SignalHandler>
  void Create(zx::eventpair* out, SignalHandler on_signal);

  // Takes one end of an event pair and starts a thread to listen for signals on it. If this object
  // was previously created or linked, it is first reset. See the above note on |on_signal|.
  template <typename SignalHandler>
  void Pair(zx::eventpair paired, SignalHandler on_signal);

  // Send one or more Zircon user signals to the other end of the eventpair. Returns true if the
  // signal was sent, or false if the other end disconnected/reset.
  bool SignalPeer(Signal signal);

  // Blocks and joins the wait loop thread. This method does not reset the eventpair, so it should
  // only be used when one side is certain the other is about to break the connection.
  void Join();

  // Calls |Join| and resets this object to its initial state, effectively breaking the connection.
  void Reset();

 private:
  void CreateImpl(zx::eventpair* out);
  void PairImpl(zx::eventpair paired);

  // Returns `ZX_EVENTPAIR_PEER_CLOSED` if disconnected, or blocks until a signal is received and
  // returns it.
  zx_signals_t WaitOne();

  template <typename SignalHandler>
  void WaitLoop(SignalHandler on_signal);

  zx::eventpair paired_;
  std::thread wait_loop_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(SignalCoordinator);
};

// Templated method implementations.

template <typename SignalHandler>
void SignalCoordinator::Create(zx::eventpair* out, SignalHandler on_signal) {
  CreateImpl(out);
  WaitLoop(on_signal);
}

template <typename SignalHandler>
void SignalCoordinator::Pair(zx::eventpair paired, SignalHandler on_signal) {
  PairImpl(std::move(paired));
  WaitLoop(on_signal);
}

template <typename SignalHandler>
void SignalCoordinator::WaitLoop(SignalHandler on_signal) {
  wait_loop_ = std::thread([this, on_signal]() {
    zx_signals_t observed;
    do {
      observed = WaitOne();
    } while (observed != ZX_EVENTPAIR_PEER_CLOSED && on_signal(observed));
    paired_.reset();
    on_signal(ZX_EVENTPAIR_PEER_CLOSED);
  });
}

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_SIGNAL_COORDINATOR_H_
