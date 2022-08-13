// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_RUNNER_H_
#define SRC_SYS_FUZZING_COMMON_RUNNER_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fit/function.h>

#include <memory>
#include <vector>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/artifact.h"
#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/monitor-clients.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/result.h"

namespace fuzzing {

using ::fuchsia::fuzzer::MonitorPtr;
using ::fuchsia::fuzzer::Status;
using ::fuchsia::fuzzer::TargetAdapter;
using ::fuchsia::fuzzer::UpdateReason;
using CorpusType = ::fuchsia::fuzzer::Corpus;

// |RunnerPtr| is the preferred way to reference a |Runner| in a future or promise without needing
// to wrap it in a scope.
class Runner;
using RunnerPtr = std::shared_ptr<Runner>;

// This base class encapsulates the logic of performing a sequence of fuzzing runs. In
// particular, it defines virtual methods for performing the fuzzing workflows asynchronously, and
// invokes those methods on a dedicated worker thread to perform them without blocking the
// controller's FIDL dispatcher thread.
class Runner {
 public:
  // Note that the destructor cannot call |Close|, |Interrupt| or |Join|, as they are virtual.
  // Instead, both this class and any derived class should have corresponding non-virtual "Impl"
  // methods and call those on destruction.
  virtual ~Runner() = default;

  // Accessors.
  const ExecutorPtr& executor() const { return executor_; }

  // Hook to allow runners to override default option values with runner-specific default values.
  virtual void OverrideDefaults(Options* options) {}

  // Add an input to the specified corpus. Returns ZX_ERR_INVALID_ARGS if |corpus_type| is
  // unrecognized.
  virtual zx_status_t AddToCorpus(CorpusType corpus_type, Input input) = 0;

  // Returns a copy of all non-empty inputs in the corpus of the given |corpus_type|.
  virtual std::vector<Input> GetCorpus(CorpusType corpus_type) = 0;

  // Parses the given |input| as an AFL-style dictionary. For format details, see
  // https://lcamtuf.coredump.cx/afl/technical_details.txt. Returns ZX_ERR_INVALID_ARGS if parsing
  // fails.
  virtual zx_status_t ParseDictionary(const Input& input) = 0;

  // Returns the current dictionary serialized into an |Input|.
  virtual Input GetDictionaryAsInput() const = 0;

  // Fuzzing workflows corresponding to methods in `fuchsia.fuzzer.Controller`.
  virtual ZxPromise<> Configure(const OptionsPtr& options) = 0;
  ZxPromise<FuzzResult> Execute(Input input);
  virtual ZxPromise<Input> Minimize(Input input) = 0;
  virtual ZxPromise<Input> Cleanse(Input input) = 0;
  virtual ZxPromise<Artifact> Fuzz() = 0;
  virtual ZxPromise<> Merge() = 0;

  // Like |Execute| above, but takes multiple |inputs| instead of one.
  virtual ZxPromise<FuzzResult> Execute(std::vector<Input> inputs) = 0;

  // Cancels the current workflow.
  virtual ZxPromise<> Stop() = 0;

  // Adds a subscriber for status updates.
  void AddMonitor(fidl::InterfaceHandle<Monitor> monitor);

  // Creates a |Status| object representing all attached processes.
  virtual Status CollectStatus() = 0;

 protected:
  // Represents a single fuzzing workflow, e.g. |Execute|, |Minimize|, etc. It holds a pointer to
  // the object that created it, but this is safe: it cannot outlive the object it is a part of.
  // It should be used in the normal way, e.g. using |wrap_with|.
  class Workflow final {
   public:
    explicit Workflow(Runner* runner) : runner_(runner) {}
    ~Workflow() = default;

    // Use |wrap_with(workflow_)| on promises that implement a workflow's behavior to create scoped
    // actions on set up and tear down.
    template <typename Promise>
    decltype(auto) wrap(Promise promise) {
      static_assert(std::is_same<typename Promise::error_type, zx_status_t>::value,
                    "Workflows must use an error type of zx_status_t.");
      return Start()
          .and_then(std::move(promise))
          .inspect([this](const typename Promise::result_type& result) { Finish(); })
          .wrap_with(scope_);
    }

    // Returns a promise to stop the current workflow. The promise completes after |Finish| is
    // called.
    ZxPromise<> Stop();

   private:
    ZxPromise<> Start();
    void Finish();

    Runner* runner_ = nullptr;
    ZxCompleter<> completer_;
    ZxConsumer<> consumer_;
    Scope scope_;
  };

  explicit Runner(ExecutorPtr executor);

  // These methods allow specific runners to implement actions that should be performed at the start
  // or end of a workflow. They are called automatically by |Workflow|. The runners may also create
  // additional tasks constrained to the workflow's |scope|.
  virtual void StartWorkflow(Scope& scope) {}
  virtual void FinishWorkflow() {}

  // Collects the current status, labels it with the given |reason|, and sends it all attached
  //|Monitor|s.
  void UpdateMonitors(UpdateReason reason);

 private:
  // Like |UpdateMonitors|, but uses UpdateReason::DONE as the reason and disconnects monitors after
  // they acknowledge receipt.
  void FinishMonitoring();

  ExecutorPtr executor_;
  MonitorClients monitors_;
  Scope scope_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(Runner);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_RUNNER_H_
