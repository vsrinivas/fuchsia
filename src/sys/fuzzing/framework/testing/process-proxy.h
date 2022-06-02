// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_FRAMEWORK_TESTING_PROCESS_PROXY_H_
#define SRC_SYS_FUZZING_FRAMEWORK_TESTING_PROCESS_PROXY_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/zx/process.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <unordered_map>
#include <vector>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/async-eventpair.h"
#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/framework/engine/module-pool.h"
#include "src/sys/fuzzing/framework/testing/module.h"

namespace fuzzing {

using ::fuchsia::fuzzer::Instrumentation;
using ::fuchsia::fuzzer::InstrumentationSyncPtr;
using ::fuchsia::fuzzer::InstrumentedProcess;
using ::fuchsia::fuzzer::LlvmModule;

// This class combines a simple implementation of |Instrumentation| with the signal coordination of
// |ProcessProxy| to create a test fixture for processes that bypasses the coverage component.
class FakeProcessProxy : public Instrumentation {
 public:
  FakeProcessProxy(ExecutorPtr executor, ModulePoolPtr pool);
  ~FakeProcessProxy() override = default;

  zx_koid_t process_koid() const { return process_koid_; }
  size_t num_modules() const { return ids_.size(); }
  bool has_module(FakeFrameworkModule* module) const;

  void Configure(OptionsPtr options);

  // FIDL methods.
  fidl::InterfaceRequestHandler<Instrumentation> GetHandler();
  void Initialize(InstrumentedProcess instrumented, InitializeCallback callback) override;
  void AddLlvmModule(LlvmModule llvm_module, AddLlvmModuleCallback callback) override;

  // Send a signal to the target process. This will complete any pending |AwaitSent| promise.
  __WARN_UNUSED_RESULT zx_status_t SignalPeer(Signal signal);

  // Returns a promise that completes when the given |signal| is received.
  Promise<> AwaitReceived(Signal signal);

  // Returns a promise that completes when the given |signal| is sent.
  Promise<> AwaitSent(Signal signal);

 private:
  fidl::Binding<Instrumentation> binding_;
  AsyncEventPair eventpair_;
  ModulePoolPtr pool_;
  OptionsPtr options_;
  zx_koid_t process_koid_ = 0;
  std::unordered_map<uint64_t, uint64_t> ids_;
  std::vector<SharedMemory> counters_;
  Completer<zx_signals_t> completer_;
  Scope scope_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(FakeProcessProxy);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_TESTING_PROCESS_PROXY_H_
