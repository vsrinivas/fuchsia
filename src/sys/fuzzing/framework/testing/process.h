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
#include "src/sys/fuzzing/common/signal-coordinator.h"
#include "src/sys/fuzzing/common/sync-wait.h"
#include "src/sys/fuzzing/framework/target/module.h"
#include "src/sys/fuzzing/framework/testing/module.h"
#include "src/sys/fuzzing/framework/testing/target.h"

namespace fuzzing {

using ::fuchsia::fuzzer::Instrumentation;
using ::fuchsia::fuzzer::InstrumentationSyncPtr;
using ::fuchsia::fuzzer::InstrumentedProcess;

// This class wraps a spawned |TestTarget| process and gives test additional control over it. Tests
// can simulate calls by the process to the |fuchsia.fuzzer.Instrumentation| and the feedback
// provided by the shared objects they exchange. Tests can also generate the target's
// |InstrumentedProcess| directly and indicate which aspects are not relevant to a test. Finally,
// test may manipulate the spawned task itself, forcing it to crash or exit.
class FakeProcess final {
 public:
  FakeProcess() = default;
  ~FakeProcess() = default;

  // Fake calls to |fuchsia.fuzzer.Instrumentation|.
  fidl::InterfaceRequest<Instrumentation> NewRequest();
  void AddProcess();
  void AddLlvmModule();

  // Fakes the feedback from a target process.
  void SetLeak(bool leak_suspected);
  void SetCoverage(const Coverage& coverage);

  // Creates |InstrumentedProcess| objects for this target.
  InstrumentedProcess IgnoreSentSignals(zx::process&& process);
  InstrumentedProcess IgnoreTarget(zx::eventpair&& eventpair);
  InstrumentedProcess IgnoreAll();

  // Causes the spawned process to exit with the given |exitcode|.
  void Exit(int32_t exitcode);

  // Crashes the spawned process, creating an exception.
  void Crash();

 private:
  zx::eventpair MakeIgnoredEventpair();
  zx::process MakeIgnoredProcess();

  void Reset();

  FakeFrameworkModule module_;
  InstrumentationSyncPtr instrumentation_;
  SignalCoordinator coordinator_;
  TestTarget target_;
  SyncWait sync_;
  bool leak_suspected_ = false;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(FakeProcess);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_TESTING_PROCESS_H_
