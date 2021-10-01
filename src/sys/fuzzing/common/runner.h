// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_RUNNER_H_
#define SRC_SYS_FUZZING_COMMON_RUNNER_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/function.h>
#include <lib/sync/completion.h>

#include <atomic>
#include <memory>
#include <thread>

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/options.h"

namespace fuzzing {

using ::fuchsia::fuzzer::MonitorPtr;
using ::fuchsia::fuzzer::Result;
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
  virtual ~Runner();

  // Accessors.
  Result result() const { return result_; }
  Input result_input() const { return result_input_.Duplicate(); }

  // Lets this objects add defaults to unspecified options.
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
  void AddMonitor(MonitorPtr monitor) FXL_LOCKS_EXCLUDED(mutex_);

  // Creates a |Status| object representing all attached processes.
  Status CollectStatus() FXL_LOCKS_EXCLUDED(mutex_);

 protected:
  Runner();

  virtual void set_result(Result result) { result_ = result; }
  virtual void set_result_input(const Input& input) { result_input_ = input.Duplicate(); }

  // Fuzzing workflow implementations.
  virtual void ConfigureImpl(const std::shared_ptr<Options>& options) = 0;
  virtual zx_status_t SyncExecute(const Input& input) = 0;
  virtual zx_status_t SyncMinimize(const Input& input) = 0;
  virtual zx_status_t SyncCleanse(const Input& input) = 0;
  virtual zx_status_t SyncFuzz() = 0;
  virtual zx_status_t SyncMerge() = 0;

  // Creates a |Status| object representing all attached processes.
  virtual Status CollectStatusLocked() = 0;

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

  std::mutex mutex_;

  // Worker variables.
  std::thread worker_;
  sync_completion_t worker_sync_;
  bool idle_ FXL_GUARDED_BY(mutex_) = false;
  uint8_t action_ FXL_GUARDED_BY(mutex_);
  Input input_ FXL_GUARDED_BY(mutex_);
  fit::function<void(zx_status_t)> callback_ FXL_GUARDED_BY(mutex_);

  // Result variables.
  Result result_ = Result::NO_ERRORS;
  Input result_input_;
  std::vector<MonitorPtr> monitors_ FXL_GUARDED_BY(mutex_);

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(Runner);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_RUNNER_H_
