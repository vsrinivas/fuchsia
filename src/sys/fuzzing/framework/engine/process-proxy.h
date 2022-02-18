// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_FRAMEWORK_ENGINE_PROCESS_PROXY_H_
#define SRC_SYS_FUZZING_FRAMEWORK_ENGINE_PROCESS_PROXY_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/process.h>
#include <stddef.h>

#include <atomic>
#include <memory>
#include <thread>
#include <unordered_map>

#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/result.h"
#include "src/sys/fuzzing/common/run-once.h"
#include "src/sys/fuzzing/common/shared-memory.h"
#include "src/sys/fuzzing/common/signal-coordinator.h"
#include "src/sys/fuzzing/framework/engine/module-pool.h"
#include "src/sys/fuzzing/framework/engine/module-proxy.h"
#include "src/sys/fuzzing/framework/target/process.h"

namespace fuzzing {

using ::fuchsia::fuzzer::InstrumentedProcess;
using ::fuchsia::fuzzer::LlvmModule;
using ::fuchsia::fuzzer::ProcessStats;

// This class presents an interface to the engine for a instrumented target process. It tracks the
// LLVM modules associated with the process and synchronizes coverage collection with fuzzing runs.
// It also monitors the process for crashes and abnormal exits.
class ProcessProxyImpl final {
 public:
  ProcessProxyImpl(uint64_t target_id, const std::shared_ptr<ModulePool>& pool);
  ~ProcessProxyImpl();

  uint64_t target_id() const { return target_id_; }
  bool leak_suspected() const { return leak_suspected_; }

  // Adds default values to unspecified options that are needed by objects of this class.
  static void AddDefaults(Options* options);

  // Sets options for this object.
  void Configure(const std::shared_ptr<Options>& options);

  // The |on_signal| callback will be invoked when the associated process acknowledges a call to
  // |Start| or |Finish|. The |on_error| callback will be invoked with the |target_id| when the
  // associated process exits, either normally or abnormally.
  using SignalHandler = fit::closure;
  using ErrorHandler = fit::function<void(uint64_t)>;
  void SetHandlers(SignalHandler on_signal, ErrorHandler on_error);

  // Coverage methods.
  void Connect(InstrumentedProcess instrumented);
  void AddLlvmModule(LlvmModule llvm_module);

  // Signals the associated process that a fuzzing run is starting and if it should |detect_leaks|.
  void Start(bool detect_leaks);

  // Signals the associated process that a fuzzing run is finishing.
  void Finish();

  // Adds the associated process' |ProcessStats| to |out|.
  zx_status_t GetStats(ProcessStats* out);

  // Waits for the process to exit, and returns the fuzzing result determined from its exit code, or
  // previously provided result.
  FuzzResult GetResult();

  // Dumps information about all threads in a process to the provided buffer. Returns the number of
  // bytes written, not including the null-terminator.
  size_t Dump(void* buffer, size_t size);

 private:
  uint64_t target_id_ = kInvalidTargetId;
  std::shared_ptr<Options> options_;

  std::shared_ptr<ModulePool> pool_;
  std::unordered_map<ModuleProxy*, SharedMemory> modules_;
  zx::process process_;

  SignalCoordinator coordinator_;
  SignalHandler on_signal_;
  ErrorHandler on_error_;

  zx::channel exception_channel_;
  std::thread exception_thread_;

  // These are accessed from both the |SignalCoordinator| and the |Runner|.
  std::atomic<bool> leak_suspected_ = false;
  std::atomic<FuzzResult> result_ = FuzzResult::NO_ERRORS;

  std::atomic<bool> closed_ = false;
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_ENGINE_PROCESS_PROXY_H_
