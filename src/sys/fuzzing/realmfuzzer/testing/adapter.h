// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_REALMFUZZER_TESTING_ADAPTER_H_
#define SRC_SYS_FUZZING_REALMFUZZER_TESTING_ADAPTER_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>
#include <zircon/compiler.h>

#include <memory>
#include <string>
#include <vector>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/async-eventpair.h"
#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/shared-memory.h"

namespace fuzzing {

using ::fuchsia::fuzzer::TargetAdapter;
using ::fuchsia::mem::Buffer;

// This class implements |fuchsia.fuzzer.TargetAdapter| for unit testing, and gives tests fine-
// grained control over the signals and test inputs exchanged with the runner.
class FakeTargetAdapter final : public TargetAdapter {
 public:
  explicit FakeTargetAdapter(ExecutorPtr executor);
  ~FakeTargetAdapter() override = default;

  // Provides a request handler for the engine to connect to the target adapter.
  fidl::InterfaceRequestHandler<TargetAdapter> GetHandler();

  // Records the command-line parameters.
  void SetParameters(const std::vector<std::string>& parameters);

  // FIDL methods.
  void GetParameters(GetParametersCallback callback) override;
  void Connect(zx::eventpair eventpair, zx::vmo test_input, ConnectCallback callback) override;

  // Returns a promise to |AwaitStart| and then |Finish|.
  ZxPromise<Input> TestOneInput();

  // Returns a promise that waits for a start signal and returns the provided test input.
  ZxPromise<Input> AwaitStart();

  // Sends a signal to the engine that indicates the target adapter is finished with a run.
  __WARN_UNUSED_RESULT zx_status_t Finish();

  // Returns a promise that waits for the client to disconnect.
  ZxPromise<> AwaitDisconnect();

 private:
  fidl::Binding<TargetAdapter> binding_;
  ExecutorPtr executor_;
  std::vector<std::string> parameters_;
  AsyncEventPair eventpair_;
  SharedMemory test_input_;
  fpromise::suspended_task suspended_;
  Scope scope_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(FakeTargetAdapter);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_REALMFUZZER_TESTING_ADAPTER_H_
