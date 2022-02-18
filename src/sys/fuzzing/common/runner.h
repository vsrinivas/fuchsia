// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_RUNNER_H_
#define SRC_SYS_FUZZING_COMMON_RUNNER_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fit/function.h>

#include <atomic>
#include <memory>
#include <thread>

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/monitors.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/result.h"
#include "src/sys/fuzzing/common/run-once.h"
#include "src/sys/fuzzing/common/sync-wait.h"

namespace fuzzing {

using ::fuchsia::fuzzer::MonitorPtr;
using ::fuchsia::fuzzer::Status;
using ::fuchsia::fuzzer::TargetAdapter;
using ::fuchsia::fuzzer::UpdateReason;
using CorpusType = ::fuchsia::fuzzer::Corpus;

// This base class encapsulates the logic of performing a sequence of fuzzing runs. In
// particular, it defines virtual methods for performing the fuzzing workflows asynchronously, and
// invokes those methods on a dedicated worker thread to perform them without blocking the
// controller's FIDL dispatcher thread.
class Runner {
 public:
  // Note that the destructor cannot call |Close|, |Interrupt| or |Join|, as they are virtual.
  // Instead, both this class and any derived class should have corresponding non-virtual "Impl"
  // methods and call those on destruction.
  virtual ~Runner();

  // Accessors.
  FuzzResult result() const { return result_; }
  Input result_input() const { return result_input_.Duplicate(); }

  // Adds default values to unspecified options that are needed by objects of this class.
  virtual void AddDefaults(Options* options) = 0;

  // Add an input to the specified corpus. Returns ZX_ERR_INVALID_ARGS if |corpus_type| is
  // unrecognized.
  virtual zx_status_t AddToCorpus(CorpusType corpus_type, Input input) = 0;

  // Returns a copy of the input at the given |offset| in the corpus of the given |corpus_type|,
  // or an emtpy input if |offset| is invalid.
  virtual Input ReadFromCorpus(CorpusType corpus_type, size_t offset) = 0;

  // Parses the given |input| as an AFL-style dictionary. For format details, see
  // https://lcamtuf.coredump.cx/afl/technical_details.txt. Returns ZX_ERR_INVALID_ARGS if parsing
  // fails.
  virtual zx_status_t ParseDictionary(const Input& input) = 0;

  // Returns the current dictionary serialized into an |Input|.
  virtual Input GetDictionaryAsInput() const = 0;

  // Fuzzing workflows.
  zx_status_t Configure(const std::shared_ptr<Options>& options);
  void Execute(Input input, fit::function<void(zx_status_t)> callback) FXL_LOCKS_EXCLUDED(mutex_);
  void Minimize(Input input, fit::function<void(zx_status_t)> callback) FXL_LOCKS_EXCLUDED(mutex_);
  void Cleanse(Input input, fit::function<void(zx_status_t)> callback) FXL_LOCKS_EXCLUDED(mutex_);
  void Fuzz(fit::function<void(zx_status_t)> callback) FXL_LOCKS_EXCLUDED(mutex_);
  void Merge(fit::function<void(zx_status_t)> callback) FXL_LOCKS_EXCLUDED(mutex_);

  // Adds a subscriber for status updates.
  void AddMonitor(fidl::InterfaceHandle<Monitor> monitor) FXL_LOCKS_EXCLUDED(mutex_);

  // Creates a |Status| object representing all attached processes.
  virtual Status CollectStatus() = 0;

  // Close any sources of new tasks. Derived classes should call their base class's method BEFORE
  // performing actions specific to the derived class.
  virtual void Close() { close_.Run(); }

  // Interrupt the current task. Calling order should not matter.
  virtual void Interrupt() { interrupt_.Run(); }

  // Join any separate threads or other asynchronous workflows. Derived classes should call their
  // base class's method AFTER performing actions specific to the derived class.
  virtual void Join() { join_.Run(); }

 protected:
  Runner();

  virtual void set_result(FuzzResult result) { result_ = result; }
  virtual void set_result_input(const Input& input) { result_input_ = input.Duplicate(); }

  // Fuzzing workflow implementations.
  virtual void ConfigureImpl(const std::shared_ptr<Options>& options) = 0;
  virtual zx_status_t SyncExecute(const Input& input) = 0;
  virtual zx_status_t SyncMinimize(const Input& input) = 0;
  virtual zx_status_t SyncCleanse(const Input& input) = 0;
  virtual zx_status_t SyncFuzz() = 0;
  virtual zx_status_t SyncMerge() = 0;

  // Resets the error state for subsequent actions.
  virtual void ClearErrors();

  // Collects the current status, labels it with the given |reason|, and sends it all attached
  //|Monitor|s.
  void UpdateMonitors(UpdateReason reason) FXL_LOCKS_EXCLUDED(mutex_);

 private:
  // Schedule a workflow to be performed by the worker thread.
  void Pend(uint8_t action, Input input, fit::function<void(zx_status_t)> callback)
      FXL_LOCKS_EXCLUDED(mutex_);

  // The worker thread body.
  void Worker() FXL_LOCKS_EXCLUDED(mutex_);

  // Like |UpdateMonitors|, but uses UpdateReason::DONE as the reason and disconnects monitors after
  // they acknowledge receipt.
  void FinishMonitoring();

  // Stop-related methods.
  void CloseImpl();
  void InterruptImpl();
  void JoinImpl();

  std::mutex mutex_;

  // Worker variables.
  std::thread worker_;
  SyncWait worker_sync_;
  bool idle_ FXL_GUARDED_BY(mutex_) = true;
  std::atomic<bool> stopped_ = false;

  uint8_t action_ FXL_GUARDED_BY(mutex_);
  Input input_ FXL_GUARDED_BY(mutex_);
  fit::function<void(zx_status_t)> callback_ FXL_GUARDED_BY(mutex_);

  // Result variables.
  FuzzResult result_ = FuzzResult::NO_ERRORS;
  Input result_input_;
  MonitorClients monitors_;

  RunOnce close_;
  RunOnce interrupt_;
  RunOnce join_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(Runner);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_RUNNER_H_
