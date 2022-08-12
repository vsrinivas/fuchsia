// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_REALMFUZZER_ENGINE_PROCESS_PROXY_H_
#define SRC_SYS_FUZZING_REALMFUZZER_ENGINE_PROCESS_PROXY_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/process.h>
#include <stddef.h>
#include <zircon/compiler.h>

#include <memory>
#include <unordered_map>

#include "src/sys/fuzzing/common/async-eventpair.h"
#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/result.h"
#include "src/sys/fuzzing/common/shared-memory.h"
#include "src/sys/fuzzing/realmfuzzer/engine/module-pool.h"
#include "src/sys/fuzzing/realmfuzzer/engine/module-proxy.h"
#include "src/sys/fuzzing/realmfuzzer/target/process.h"

namespace fuzzing {

using ::fuchsia::fuzzer::InstrumentedProcess;
using ::fuchsia::fuzzer::ProcessStats;

// This class presents an interface to the engine for a instrumented target process. It tracks the
// LLVM modules associated with the process and synchronizes coverage collection with fuzzing runs.
// It also monitors the process for crashes and abnormal exits.
class ProcessProxy final {
 public:
  ProcessProxy(ExecutorPtr executor, const ModulePoolPtr& pool);
  ~ProcessProxy();

  uint64_t target_id() const { return target_id_; }

  // Sets options for this object.
  void Configure(const OptionsPtr& options);

  // ControllerDataCollector-related methods.
  __WARN_UNUSED_RESULT zx_status_t Connect(InstrumentedProcess& instrumented);
  __WARN_UNUSED_RESULT zx_status_t AddModule(zx::vmo& inline_8bit_counters);

  // Signals the associated process that a fuzzing run is starting and if it should |detect_leaks|.
  // Returns a promise that completes when the process acknowledges the signal.
  ZxPromise<> Start(bool detect_leaks);

  // Signals the associated process that a fuzzing run is finishing.
  __WARN_UNUSED_RESULT zx_status_t Finish();

  // Returns a promise that completes either when the process acknowledges a (possibly subsequent)
  // call to |Finish|, or when it encounters an error. The promise returns whether any memory leaks
  // are suspected when successful, and the proxy's target ID on error.
  Promise<bool, uint64_t> AwaitFinish();

  // Adds the associated process' |ProcessStats| to |out|.
  __WARN_UNUSED_RESULT zx_status_t GetStats(ProcessStats* out);

  // Promises to return the fuzzing result from a process that encountered a fatal error. Waits for
  // the process to terminate.
  ZxPromise<FuzzResult> GetResult();

  // Dumps information about all threads in a process to the provided buffer. Returns the number of
  // bytes written, not including the null-terminator.
  size_t Dump(void* buffer, size_t size);

 private:
  // Sets signals for the process. This can be used to inform the process when fuzzing is starting
  // or stopping.
  void SignalPeer(Signal signal);

  // Promises to return the next received signals from the process, or an error if the process
  // encounters a fatal error.
  Promise<zx_signals_t> AwaitSignals();

  ExecutorPtr executor_;
  uint64_t target_id_ = kInvalidTargetId;
  OptionsPtr options_;

  AsyncEventPair eventpair_;
  ModulePoolPtr pool_;
  std::unordered_map<ModuleProxy*, SharedMemory> modules_;
  zx::process process_;

  // This is accessed from both the |ExecutorPtr| and the |Runner|.
  FuzzResult result_ = FuzzResult::NO_ERRORS;

  Scope scope_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(ProcessProxy);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_REALMFUZZER_ENGINE_PROCESS_PROXY_H_
