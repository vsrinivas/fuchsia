// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_FRAMEWORK_TESTING_ADAPTER_H_
#define SRC_SYS_FUZZING_FRAMEWORK_TESTING_ADAPTER_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sync/completion.h>

#include <memory>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/binding.h"
#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/shared-memory.h"
#include "src/sys/fuzzing/common/signal-coordinator.h"
#include "src/sys/fuzzing/common/testing/dispatcher.h"

namespace fuzzing {

using ::fuchsia::fuzzer::TargetAdapter;
using ::fuchsia::mem::Buffer;

// This class implements |fuchsia.fuzzer.TargetAdapter| for unit testing, and gives tests fine-
// grained control over the signals and test inputs exchanged with the runner.
class FakeTargetAdapter final : public TargetAdapter {
 public:
  FakeTargetAdapter();
  ~FakeTargetAdapter() override = default;

  // Returns the contents of the last test input provided by the engine.
  Input test_input() { return Input(test_input_); }

  // Provides a request handler for the engine to connect to the target adapter.
  fidl::InterfaceRequestHandler<TargetAdapter> GetHandler();

  // Mimics a call to |fuchsia.fuzzer.TargetAdapter.Connect|.
  void Connect(zx::eventpair eventpair, Buffer test_input, ConnectCallback callback) override;

  // Waits for a signal from the engine.
  zx_signals_t AwaitSignal();

  // Sends a signal to the engine.
  void SignalPeer(Signal signal);

 private:
  Binding<TargetAdapter> binding_;
  FakeDispatcher dispatcher_;
  SharedMemory test_input_;
  SignalCoordinator coordinator_;

  // When the adapter receives a signal, it blocks on |wsync_|, stores it in |observed_|, and
  // signals |rsync_|. When |AwaitSignals| is called, it blocks on |rsync_|, reads |obeserved_|, and
  // signals |wsync_|. In this way, each received signal is paired with a call to |AwaitSignal|.
  zx_signals_t observed_;
  sync_completion_t rsync_;
  sync_completion_t wsync_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(FakeTargetAdapter);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_TESTING_ADAPTER_H_
