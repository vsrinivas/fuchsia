// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_FRAMEWORK_COVERAGE_FORWARDER_H_
#define SRC_SYS_FUZZING_FRAMEWORK_COVERAGE_FORWARDER_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include <memory>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/async-deque.h"
#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/framework/coverage/instrumentation.h"
#include "src/sys/fuzzing/framework/coverage/provider.h"
#include "src/sys/fuzzing/framework/target/process.h"

namespace fuzzing {

using ::fuchsia::fuzzer::CoverageProvider;
using ::fuchsia::fuzzer::Instrumentation;

class CoverageForwarder final {
 public:
  explicit CoverageForwarder(ExecutorPtr executor);
  ~CoverageForwarder() = default;

  // FIDL protocol handlers.
  fidl::InterfaceRequestHandler<Instrumentation> GetInstrumentationHandler();
  fidl::InterfaceRequestHandler<CoverageProvider> GetCoverageProviderHandler();

 private:
  uint64_t last_target_id_ = kTimeoutTargetId;
  ExecutorPtr executor_;
  OptionsPtr options_;
  AsyncDequePtr<CoverageEvent> events_;
  fidl::BindingSet<Instrumentation, std::unique_ptr<InstrumentationImpl>> instrumentations_;
  std::unique_ptr<CoverageProviderImpl> provider_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(CoverageForwarder);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_COVERAGE_FORWARDER_H_
