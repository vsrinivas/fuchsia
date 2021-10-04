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

#include <memory>
#include <thread>
#include <unordered_map>

#include "src/sys/fuzzing/common/binding.h"
#include "src/sys/fuzzing/common/dispatcher.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/shared-memory.h"
#include "src/sys/fuzzing/common/signal-coordinator.h"
#include "src/sys/fuzzing/framework/engine/module-pool.h"
#include "src/sys/fuzzing/framework/engine/module-proxy.h"

namespace fuzzing {

using ::fuchsia::fuzzer::Feedback;
using ::fuchsia::fuzzer::ProcessProxy;
using ::fuchsia::fuzzer::ProcessStats;
using ::fuchsia::fuzzer::Result;

// This class interacts with libFuzzer's FuzzerProxy and forwards calls to and from the "remote"
// portion of libFuzzer's FuzzerRemoteInterface.h for a specific remote fuzzing process. It also
// implements the fuchsia.fuzzer.Proxy  FIDL protocols, allowing the remote fuzzing process to
// provide its coverage data and notify the fuzzing engine of errors.
class ProcessProxyImpl final : public ProcessProxy {
 public:
  ProcessProxyImpl(const std::shared_ptr<Dispatcher>& dispatcher,
                   const std::shared_ptr<ModulePool>& pool);
  ~ProcessProxyImpl() override;

  bool leak_suspected() const { return leak_suspected_; }

  // Lets this objects add defaults to unspecified options.
  static void AddDefaults(Options* options);

  // Sets options for this object.
  void Configure(const std::shared_ptr<Options>& options);

  // The |on_signal|  callback will be invoked when the associated process acknowledges a call to
  // |Start| or |Finish|. The |on_error| callback will be invoked with the current object when the
  // associated process exits, either normally or abnormally.
  using SignalHandler = fit::closure;
  using ErrorHandler = fit::function<void(ProcessProxyImpl*)>;
  void SetHandlers(SignalHandler on_signal, ErrorHandler on_error);

  // Binds the FIDL interface request to this object.
  void Bind(fidl::InterfaceRequest<ProcessProxy> request);

  // FIDL methods
  void Connect(zx::eventpair eventpair, zx::process process, ConnectCallback callback) override;
  void AddFeedback(Feedback feedback, AddFeedbackCallback callback) override;

  // Signals the associated process that a fuzzing run is starting or finishing.
  void Start(bool detect_leaks);
  void Finish();

  // Adds the associated process' |ProcessStats| to |out|.
  zx_status_t GetStats(ProcessStats* out);

  // Dumps information about all threads in a process to the provided buffer. Returns the number of
  // bytes written, not including the null-terminator.
  size_t Dump(void* buffer, size_t size);

  // Kills the associated process.
  void Kill();

  // Waits for the process to exit, and returns the fuzzing result determined from its exit code, or
  // previously provided result.
  Result Join();

 private:
  std::shared_ptr<Options> options_;
  Binding<ProcessProxy> binding_;

  std::shared_ptr<ModulePool> pool_;
  std::unordered_map<ModuleProxy*, SharedMemory> modules_;
  zx::process process_;

  SignalCoordinator coordinator_;
  SignalHandler on_signal_;
  ErrorHandler on_error_;

  zx::channel exception_channel_;
  std::thread exception_thread_;

  bool leak_suspected_ = false;
  Result result_ = Result::NO_ERRORS;
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_ENGINE_PROCESS_PROXY_H_
