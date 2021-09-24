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

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/binding.h"
#include "src/sys/fuzzing/common/shared-memory.h"
#include "src/sys/fuzzing/common/signal-coordinator.h"

// Fuzz target function provided by the user.
__EXPORT extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

namespace fuzzing {

using ::fuchsia::fuzzer::TargetAdapter;
using ::fuchsia::mem::Buffer;

class LLVMTargetAdapter final : public TargetAdapter {
 public:
  LLVMTargetAdapter();
  ~LLVMTargetAdapter() override = default;

  // Returns an interface request handler. The given |on_close| closure will be invoked when a
  // |Connect|ed peer, i.e. the engine, disconnects.
  fidl::InterfaceRequestHandler<TargetAdapter> GetHandler(fit::closure on_close,
                                                          async_dispatcher_t* dispatcher);

  // FIDL method.
  void Connect(zx::eventpair eventpair, Buffer test_input, ConnectCallback callback) override;

 private:
  bool OnSignal(zx_signals_t observed);

  Binding<TargetAdapter> binding_;
  SignalCoordinator coordinator_;
  fit::closure on_close_;
  SharedMemory test_input_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(LLVMTargetAdapter);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_ADAPTERS_LLVM_H_
