// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FUZZING_FIDL_LLVM_FUZZER_H_
#define SRC_LIB_FUZZING_FIDL_LLVM_FUZZER_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding_set.h>
#include <zircon/types.h>

#include <string>
#include <vector>

#include "test-input.h"

namespace fuzzing {
namespace {

using fuchsia::fuzzer::DataProviderPtr;
using fuchsia::fuzzer::LlvmFuzzer;

}  // namespace

class LlvmFuzzerImpl : public LlvmFuzzer {
 public:
  LlvmFuzzerImpl();
  virtual ~LlvmFuzzerImpl();

  // Returns a request handler that can be used to connect the engine to this instance. Only the
  // first request will be handled; subsequent requests are ignored.
  fidl::InterfaceRequestHandler<LlvmFuzzer> GetHandler(async_dispatcher_t *dispatcher = nullptr);

  // FIDL methods
  void Initialize(zx::vmo vmo, std::vector<std::string> options,
                  InitializeCallback callback) override;
  void TestOneInput(TestOneInputCallback callback) override;

  // Resets the object to an initial state.
  void Reset();

 private:
  // Bindings for the connected engine.
  fidl::Binding<LlvmFuzzer> binding_;

  // Test input data.
  TestInput input_;
};

}  // namespace fuzzing

#endif  // SRC_LIB_FUZZING_FIDL_LLVM_FUZZER_H_
