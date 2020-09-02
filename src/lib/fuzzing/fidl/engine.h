// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FUZZING_FIDL_ENGINE_H_
#define SRC_LIB_FUZZING_FIDL_ENGINE_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fxl/macros.h>
#include <lib/sync/completion.h>
#include <lib/sys/cpp/component_context.h>
#include <stddef.h>
#include <stdint.h>

#include <memory>

#include "coverage.h"
#include "data-provider.h"

namespace fuzzing {
namespace {

using ::fuchsia::fuzzer::Engine;
using ::fuchsia::fuzzer::LlvmFuzzerPtr;

}  // namespace

// This class integrates the Coverage, DataProvider, and LlvmFuzzer FIDL services into a single
// object that can perform fuzzing iterations. See also libfuzzer.cc, which integrates the
// libFuzzer compiler runtime with this class.
class EngineImpl : public Engine {
 public:
  // Singleton. Tests can avoid the engine starting a dispatch loop by calling this with
  // |autoconnect| set to false before any other calls, preferably by using |UseContext| below.
  static EngineImpl *GetInstance(bool autoconnect = true);
  virtual ~EngineImpl();

  // Helper for getting a test instance of Engine that uses the test's |context| and |dispatcher|.
  static void UseContext(std::unique_ptr<sys::ComponentContext> context) {
    GetInstance(false)->UseContextImpl(std::move(context));
  }

  // Accessors
  AggregatedCoverage &coverage() { return coverage_; }
  DataProviderImpl &data_provider() { return data_provider_; }

  // Sets the LlvmFuzzer service this engine is connected to. Used for testing (autoconnect=false).
  zx_status_t SetLlvmFuzzer(LlvmFuzzerPtr fuzzer);

  // FIDL methods
  void Start(std::vector<std::string> options, StartCallback callback) override;

  // LLVM C ABI functions
  // See https://github.com/llvm/llvm-project/blob/master/compiler-rt/lib/fuzzer/FuzzerInterface.h
  int Initialize(int *argc, char ***argv);
  int TestOneInput(const uint8_t *data, size_t size);

  // Stops the engine and invokes the callback passed to |Start|.
  void Stop(zx_status_t status);

 private:
  explicit EngineImpl(bool autoconnect);

  void UseContextImpl(std::unique_ptr<sys::ComponentContext> context);

  // FIDL dispatcher loop. Null when testing.
  std::unique_ptr<async::Loop> loop_;

  std::unique_ptr<sys::ComponentContext> context_;
  async_dispatcher_t *dispatcher_;

  AggregatedCoverage coverage_;
  DataProviderImpl data_provider_;
  LlvmFuzzerPtr llvm_fuzzer_;

  // LibFuzzer options and a command line used for passing them to libFuzzer.
  // See https://llvm.org/docs/LibFuzzer.html#options
  std::vector<std::string> options_;
  std::vector<char *> argv_;

  // Blocks |Initialize| and |TestOneInput|| until |Start| has been called.
  sync_completion_t sync_;

  // Callback used to return status when the engine stops.
  StartCallback callback_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(EngineImpl);
};

}  // namespace fuzzing

#endif  // SRC_LIB_FUZZING_FIDL_ENGINE_H_
