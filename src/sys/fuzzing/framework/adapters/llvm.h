// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_FRAMEWORK_ADAPTERS_LLVM_H_
#define SRC_SYS_FUZZING_FRAMEWORK_ADAPTERS_LLVM_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/function.h>
#include <lib/zx/eventpair.h>
#include <zircon/compiler.h>

#include <memory>
#include <string>
#include <vector>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/binding.h"
#include "src/sys/fuzzing/common/shared-memory.h"
#include "src/sys/fuzzing/common/signal-coordinator.h"
#include "src/sys/fuzzing/common/sync-wait.h"

// Fuzz target function provided by the user.
__EXPORT extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

namespace fuzzing {

using ::fuchsia::fuzzer::TargetAdapter;
using ::fuchsia::mem::Buffer;

class LLVMTargetAdapter final : public TargetAdapter {
 public:
  LLVMTargetAdapter();
  ~LLVMTargetAdapter() override;

  async_dispatcher_t* dispatcher() const { return binding_.dispatcher()->get(); }

  // Returns an interface request handler.
  fidl::InterfaceRequestHandler<TargetAdapter> GetHandler();

  // Records the command-line parameters.
  void SetParameters(const std::vector<std::string>& parameters);

  // FIDL methods.
  void GetParameters(GetParametersCallback callback) override;
  void Connect(zx::eventpair eventpair, Buffer test_input, ConnectCallback callback) override;

  // Blocks until a client connects, then blocks until the channel closes.
  zx_status_t Run();

 private:
  bool OnSignal(zx_signals_t observed);

  Binding<TargetAdapter> binding_;
  SyncWait connected_;
  std::vector<std::string> parameters_;
  SignalCoordinator coordinator_;
  SharedMemory test_input_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(LLVMTargetAdapter);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_ADAPTERS_LLVM_H_
