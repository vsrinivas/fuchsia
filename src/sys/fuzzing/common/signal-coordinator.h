// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_SIGNAL_COORDINATOR_H_
#define SRC_SYS_FUZZING_COMMON_SIGNAL_COORDINATOR_H_

#include <lib/fit/function.h>
#include <lib/zx/eventpair.h>
#include <zircon/types.h>

#include <thread>

#include "src/lib/fxl/macros.h"

namespace fuzzing {

constexpr zx_signals_t kStartSignal = ZX_USER_SIGNAL_0;
constexpr zx_signals_t kFinishSignal = ZX_USER_SIGNAL_1;
constexpr zx_signals_t kLeakSignal = ZX_USER_SIGNAL_2;

// This enum renames some Zircon user signals to associate them with certain actions performed by
// the engine.
enum Signal : zx_signals_t {
  // Sent by the engine to the targets at the start of a fuzzing run, and echoed by the targets back
  // to the engine as acknowledgement.
  kStart = kStartSignal,

  // Sent by the engine to the targets at the end of a fuzzing run. Targets will echo with the same
  // or with |kFinishWithLeaks|, depending on whether they suspect a memory leak.
  kFinish = kFinishSignal,

  // Sent by the engine to the targets at the start of a fuzzing run in which leak detection should
  // be enabled. Targets will acknowledge with |kStart|.
  kStartLeakCheck = kStartSignal | kLeakSignal,

  // Sent by the targets to acknowledge receiving |kFinish| when a memory leak is suspected.
  kFinishWithLeaks = kFinishSignal | kLeakSignal,
};

// This class wraps an eventpair and thread to present a simple way for one process to signal
// another, and have that process respond. This class is used in this library with
// |fuzzing::Signals|.
class SignalCoordinator final {
 public:
  SignalCoordinator() = default;
  ~SignalCoordinator();

  bool is_valid() const;

  // Both |Create| and |Pair| take an |on_signal| parameter, which should be callable with the
  // signature: bool on_signal(zx_signals_t signals);
  //
  // This will be called when a the other end of the event pair sends a Zircon user signal to this
  // end. If this method returns false, the wait loop will exit. When the wait loop exits for any
  // reason, this method will be called one final time with |ZX_EVENTPAIR_PEER_CLOSED|.
  using SignalHandler = fit::function<bool(zx_signals_t)>;

  // Creates an event pair and returns one end via |out|. If this object was previously created or
  // linked, it is first reset. See the above note on |on_signal|.
  zx::eventpair Create(SignalHandler on_signal);

  // Takes one end of an event pair and starts a thread to listen for signals on it. If this object
  // was previously created or linked, it is first reset. See the above note on |on_signal|.
  void Pair(zx::eventpair paired, SignalHandler on_signal);

  // Sends a signal to the other end of the eventpair. Returns true if the signal was sent, or false
  // if the other end disconnected/reset.
  bool SignalPeer(Signal signal);

  // Blocks and joins the wait loop thread. This method does not reset the eventpair, so it should
  // only be used when one side is certain the other is about to break the connection.
  void Join();

  // Calls |Join| and resets this object to its initial state, effectively breaking the connection.
  void Reset();

 private:
  // Waits to receive signals and dispatches them to the previously provided |SignalHandler|.
  void WaitLoop();

  zx::eventpair paired_;
  std::thread wait_loop_;
  SignalHandler on_signal_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(SignalCoordinator);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_SIGNAL_COORDINATOR_H_
