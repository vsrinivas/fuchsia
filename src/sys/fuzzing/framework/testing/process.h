// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_FRAMEWORK_TESTING_PROCESS_H_
#define SRC_SYS_FUZZING_FRAMEWORK_TESTING_PROCESS_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fidl/cpp/interface_request.h>
#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <vector>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/async-eventpair.h"
#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/framework/target/module.h"
#include "src/sys/fuzzing/framework/testing/module.h"
#include "src/sys/fuzzing/framework/testing/target.h"

namespace fuzzing {

using ::fuchsia::fuzzer::Instrumentation;
using ::fuchsia::fuzzer::InstrumentationPtr;
using ::fuchsia::fuzzer::InstrumentedProcess;

// This class wraps a spawned |TestTarget| process and gives test additional control over it. Tests
// can simulate calls by the process to the |fuchsia.fuzzer.Instrumentation| and the feedback
// provided by the shared objects they exchange. Tests can also generate the target's
// |InstrumentedProcess| directly and indicate which aspects are not relevant to a test. Finally,
// test may manipulate the spawned task itself, forcing it to crash or exit.
class FakeProcess final {
 public:
  explicit FakeProcess(ExecutorPtr executor);
  ~FakeProcess() = default;

  bool is_running() const { return running_; }

  // TODO(fxbug.dev/92490): Replace with InstrumentationClient::RequestHandler.
  using RequestHandler = fidl::InterfaceRequestHandler<Instrumentation>;
  void set_handler(RequestHandler handler) { handler_ = std::move(handler); }

  // Returns a promise to launch a target process and fake the necessary calls to provide
  // |Instrumentation|. Does nothing if the target process is already running.
  ZxPromise<> Launch();

  // Fakes the feedback from a target process.
  void SetLeak(bool leak_suspected);
  void SetCoverage(const Coverage& coverage);

  // Creates |InstrumentedProcess| objects for this target.
  InstrumentedProcess IgnoreSentSignals(zx::process&& process);
  InstrumentedProcess IgnoreTarget(zx::eventpair&& eventpair);
  InstrumentedProcess IgnoreAll();

  // Returns a promises that waits for the engine to signal a fuzzing run is finishing. The process
  // will automatically update its coverage and respond.
  ZxPromise<> AwaitFinish();

  // Returns a promise to causes the spawned process to exit with the given |exitcode|.
  ZxPromise<> ExitAsync(int32_t exitcode);

  // Returns a promise to crash the spawned process and create an exception.
  ZxPromise<> CrashAsync();

 private:
  // Returns a promises that repeatedly waits for the engine to signal a fuzzing run is starting.
  // The process will automatically prepare its coverage and respond. The promise completes when the
  // process |Exit|s or |Crash|es.
  ZxPromise<> AwaitStart();

  ExecutorPtr executor_;
  RequestHandler handler_;
  AsyncEventPair eventpair_;
  FakeFrameworkModule module_;
  InstrumentationPtr instrumentation_;
  TestTarget target_;
  bool running_ = false;
  bool leak_suspected_ = false;
  Scope scope_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(FakeProcess);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_TESTING_PROCESS_H_
