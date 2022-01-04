// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_FRAMEWORK_COVERAGE_FORWARDER_H_
#define SRC_SYS_FUZZING_FRAMEWORK_COVERAGE_FORWARDER_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include <atomic>
#include <memory>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/dispatcher.h"
#include "src/sys/fuzzing/framework/coverage/event-queue.h"
#include "src/sys/fuzzing/framework/coverage/provider.h"
#include "src/sys/fuzzing/framework/target/process.h"

namespace fuzzing {

using ::fuchsia::fuzzer::CoverageProvider;
using ::fuchsia::fuzzer::Instrumentation;

class CoverageForwarder final {
 public:
  CoverageForwarder();
  ~CoverageForwarder() = default;

  async_dispatcher_t* dispatcher() const { return dispatcher_.get(); }

  // FIDL protocol handlers.
  fidl::InterfaceRequestHandler<Instrumentation> GetInstrumentationHandler();
  fidl::InterfaceRequestHandler<CoverageProvider> GetCoverageProviderHandler();

  // Blocks until a |CoverageProvider| client connects, then blocks until it disconnects.
  void Run();

 private:
  uint64_t last_target_id_ = kInvalidTargetId;
  Dispatcher dispatcher_;
  fidl::BindingSet<Instrumentation, std::unique_ptr<Instrumentation>> instrumentations_;
  std::unique_ptr<CoverageProviderImpl> provider_;
  std::shared_ptr<CoverageEventQueue> events_;
  std::atomic<bool> closing_ = false;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(CoverageForwarder);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_COVERAGE_FORWARDER_H_
