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
#include "src/sys/fuzzing/framework/target/module.h"
#include "src/sys/fuzzing/framework/testing/module.h"
#include "src/sys/fuzzing/framework/testing/target.h"

namespace fuzzing {

using ::fuchsia::fuzzer::ProcessProxy;
using ::fuchsia::fuzzer::ProcessProxySyncPtr;

// This class directly creates a |ProcessProxyImpl| as if it were connected to a |Process|. It gives
// tests fine-grained control over feedback exchanged with the process, and over the spawned test
// process. It mimics the FIDL interfaces without any actual dispatching.
class FakeProcess final {
 public:
  FakeProcess() = default;
  ~FakeProcess() = default;

  // Mimics a new request for |fuchsia.fuzzer.ProcessProxy|.
  fidl::InterfaceRequest<ProcessProxy> NewRequest();

  // Mimics a call to |fuchsia.fuzzer.ProcessProxy.Connect|.
  void Connect();

  // Mimics a call to |fuchsia.fuzzer.ProcessProxy.AddFeedback|.
  void AddFeedback();

  // Fakes the appearance of mismatched malloc/frees.
  void SetLeak(bool leak_suspected);

  // Sets the inline, 8-bit code coverage counters.
  void SetCoverage(const Coverage& coverage);

  // Causes the spawned process to exit with the given |exitcode|.
  void Exit(int32_t exitcode);

  // Crashes the spawned process, creating an exception.
  void Crash();

 private:
  void Reset();

  FakeModule module_;
  ProcessProxySyncPtr proxy_;
  SignalCoordinator coordinator_;
  TestTarget target_;
  bool leak_suspected_ = false;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(FakeProcess);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_TESTING_PROCESS_H_
