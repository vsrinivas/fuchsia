// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_REALMFUZZER_ADAPTERS_LLVM_H_
#define SRC_SYS_FUZZING_REALMFUZZER_ADAPTERS_LLVM_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/zx/eventpair.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/compiler.h>

#include <string>
#include <vector>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/async-eventpair.h"
#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/shared-memory.h"

// Fuzz target function provided by the user.
__EXPORT extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

namespace fuzzing {

using ::fuchsia::fuzzer::TargetAdapter;
using ::fuchsia::mem::Buffer;

class LLVMTargetAdapter final : public TargetAdapter {
 public:
  explicit LLVMTargetAdapter(ExecutorPtr executor);
  ~LLVMTargetAdapter() override = default;

  // Returns an interface request handler.
  fidl::InterfaceRequestHandler<TargetAdapter> GetHandler();

  // Records the command-line parameters.
  void SetParameters(const std::vector<std::string>& parameters);

  // FIDL methods.
  void GetParameters(GetParametersCallback callback) override;
  void Connect(zx::eventpair eventpair, zx::vmo test_input, ConnectCallback callback) override;

  // Returns a promise to perform fuzzing runs in a loop. The promise completes when the engine
  // disconnects. If the engine is not connected when this method is called, it will not complete
  // until after |Connect| is called.
  Promise<> Run();

 private:
  fidl::Binding<TargetAdapter> binding_;
  ExecutorPtr executor_;
  std::vector<std::string> parameters_;
  AsyncEventPair eventpair_;
  SharedMemory test_input_;
  Scope scope_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(LLVMTargetAdapter);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_REALMFUZZER_ADAPTERS_LLVM_H_
