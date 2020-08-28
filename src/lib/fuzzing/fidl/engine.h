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

using ::fuchsia::fuzzer::LlvmFuzzerPtr;

}  // namespace

// This class integrates the Coverage, DataProvider, and LlvmFuzzer FIDL services into a single
// object that can perform fuzzing iterations. See also libfuzzer.cc, which integrates the
// libFuzzer compiler runtime with this class.
class Engine {
 public:
  Engine();
  virtual ~Engine();

  AggregatedCoverage &coverage() { return coverage_; }
  DataProviderImpl &data_provider() { return data_provider_; }

  // Performs one fuzzing iteration.
  int RunOne(const uint8_t *data, size_t size);

 protected:
  // Test constructor
  explicit Engine(std::unique_ptr<sys::ComponentContext> context, async_dispatcher_t *dispatcher);
  friend class EngineTest;

 private:
  // FIDL dispatcher loop. Null when testing.
  std::unique_ptr<async::Loop> loop_;

  std::unique_ptr<sys::ComponentContext> context_;
  async_dispatcher_t *dispatcher_;

  AggregatedCoverage coverage_;
  DataProviderImpl data_provider_;
  LlvmFuzzerPtr llvm_fuzzer_;

  // Used to wait for a fuzzing iteration to complete, and then return the result.
  sync_completion_t sync_;
  int result_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(Engine);
};

}  // namespace fuzzing

#endif  // SRC_LIB_FUZZING_FIDL_ENGINE_H_
