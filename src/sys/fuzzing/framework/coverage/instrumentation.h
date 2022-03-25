// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_FRAMEWORK_COVERAGE_INSTRUMENTATION_H_
#define SRC_SYS_FUZZING_FRAMEWORK_COVERAGE_INSTRUMENTATION_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <stdint.h>

#include "src/sys/fuzzing/common/async-deque.h"
#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/options.h"

namespace fuzzing {

using ::fuchsia::fuzzer::CoverageEvent;
using ::fuchsia::fuzzer::Instrumentation;
using ::fuchsia::fuzzer::InstrumentedProcess;
using ::fuchsia::fuzzer::LlvmModule;

class InstrumentationImpl : public Instrumentation {
 public:
  InstrumentationImpl(uint64_t target_id, OptionsPtr options, AsyncDequePtr<CoverageEvent> events);
  ~InstrumentationImpl() override = default;

  // FIDL methods.
  void Initialize(InstrumentedProcess instrumented, InitializeCallback callback) override;
  void AddLlvmModule(LlvmModule llvm_module, AddLlvmModuleCallback callback) override;

 private:
  uint64_t target_id_;
  OptionsPtr options_;
  AsyncDequePtr<CoverageEvent> events_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(InstrumentationImpl);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_COVERAGE_INSTRUMENTATION_H_
