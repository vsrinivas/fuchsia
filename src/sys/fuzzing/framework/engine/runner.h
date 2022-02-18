// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_FRAMEWORK_ENGINE_RUNNER_H_
#define SRC_SYS_FUZZING_FRAMEWORK_ENGINE_RUNNER_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/time.h>
#include <stddef.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/sys/fuzzing/common/dispatcher.h"
#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/run-once.h"
#include "src/sys/fuzzing/common/runner.h"
#include "src/sys/fuzzing/common/shared-memory.h"
#include "src/sys/fuzzing/common/signal-coordinator.h"
#include "src/sys/fuzzing/common/sync-wait.h"
#include "src/sys/fuzzing/framework/engine/adapter-client.h"
#include "src/sys/fuzzing/framework/engine/corpus.h"
#include "src/sys/fuzzing/framework/engine/coverage-client.h"
#include "src/sys/fuzzing/framework/engine/module-pool.h"
#include "src/sys/fuzzing/framework/engine/mutagen.h"
#include "src/sys/fuzzing/framework/engine/process-proxy.h"

namespace fuzzing {

// The concrete implementation of |Runner|.
class RunnerImpl final : public Runner {
 public:
  RunnerImpl();
  ~RunnerImpl() override;

  void SetTargetAdapter(std::unique_ptr<TargetAdapterClient> target_adapter);
  void SetCoverageProvider(std::unique_ptr<CoverageProviderClient> coverage_provider);

  // |Runner| method implementations.
  void AddDefaults(Options* options) override;
  zx_status_t AddToCorpus(CorpusType corpus_type, Input input) override;
  Input ReadFromCorpus(CorpusType corpus_type, size_t offset) override;
  zx_status_t ParseDictionary(const Input& input) override;
  Input GetDictionaryAsInput() const override;
  Status CollectStatus() override FXL_LOCKS_EXCLUDED(mutex_);

  // Callback for signals received from the target adapter and process proxies that are used to
  // notify the runner that they have started or finished.
  bool OnSignal();

  // Callback for signals received from the target adapter and process proxies that are used to
  // notify the runner that they have encountered an error. Error values are interpreted as:
  //   * 0: -          no error.
  //   * UINTPTR_MAX:  timeout
  //   * other:        target_id of process proxy with error.
  void OnError(uint64_t error);

  // Stages of stopping: close sources of new tasks, interrupt the current task, and join it.
  void Close() override { close_.Run(); }
  void Interrupt() override { interrupt_.Run(); }
  void Join() override { join_.Run(); }

 protected:
  // Fuzzing workflow implementations.
  void ConfigureImpl(const std::shared_ptr<Options>& options) override;
  zx_status_t SyncExecute(const Input& input) override;
  zx_status_t SyncMinimize(const Input& input) override;
  zx_status_t SyncCleanse(const Input& input) override;
  zx_status_t SyncFuzz() override;
  zx_status_t SyncMerge() override;

  void ClearErrors() override;

 private:
  // Creates and returns a scope object for a synchronous workflow. This will reset errors,
  // deadlines, and run counts, and update monitors with an INIT update. When the object falls out
  // of scope, it will ensure the fuzzer is stopped, disable timers, and send a DONE update. Each
  // fuzzing workflow should begin with a call to this function.
  fit::deferred_action<fit::closure> SyncScope() FXL_LOCKS_EXCLUDED(mutex_);

  // Resets the timer thread's alarm, setting a new run deadline and delaying how long until the
  // TIMEOUT error is triggered.
  void ResetTimer() FXL_LOCKS_EXCLUDED(mutex_);

  // The timer thread body.
  void Timer() FXL_LOCKS_EXCLUDED(mutex_);

  // Sends the test input to the target adapter.
  void TestOne(const Input& input);

  // Sends each test input in the given |corpus| to the target adapter in turn.
  void TestCorpus(const std::shared_ptr<Corpus>& corpus);

  // The core loop, implemented two ways. In both, the loop will repeatedly generate the
  // |next_input|, send it to the target adapter, and perform additional actions using |finish_run|.
  // The "strict" version will always analyze the feedback from the Nth input before generating the
  // N+1th input. The "relaxed" version will generate the N+1th input *before* analyzing the
  // feedback of the Nth input. If the next input doesn't directly depend on the previous input's
  // feedback, this can improve performance by allowing the engine to generate inputs while waiting
  // for a run to progress.
  //
  // Parameters:
  //   next_input:    function that takes a boolean indicating if this is the first run, and returns
  //                  the next input to send to target adapter, or null if fuzzing should stop.
  //   finish_run:    function that takes the input that was just exercised by the target adapter.
  //   ignore_errors: bool that may be set to true if a workflow expects to encounter errors and
  //                  then continue, e.g. Cleanse and Merge.
  //
  void FuzzLoopStrict(fit::function<Input*(bool /* first*/)> next_input,
                      fit::function<void(Input* /* last_input */)> finish_run,
                      bool ignore_errors = false);
  void FuzzLoopRelaxed(fit::function<Input*(bool /* first*/)> next_input,
                       fit::function<void(Input* /* last_input */)> finish_run,
                       bool ignore_errors = false);

  // A loop that handles signalling the target adapter and proxies. This is started on a dedicated
  // thread by one of |FuzzLoop*|s above, allowing it to generate inputs and analyze feedback while
  // waiting for other processes to respond.
  void RunLoop(bool ignore_errors) FXL_LOCKS_EXCLUDED(mutex_);

  // Wraps |FuzzLoopStrict| to perform "normal" fuzzing, i.e. mutates an input from the live corpus,
  // accumulates feedback from each input, and exits on error, max runs, or max time.
  //
  // TODO(fxbug.dev/84364): |FuzzLoopRelaxed| is preferred here, but using that causes some test
  // flake. Switch to that version once the source of it is resolved.
  void FuzzLoop();

  // Uses the target adapter request handler to connect to the target adapter.
  void ConnectTargetAdapter();

  // Resets |sync|, but only if there is no pending error, allowing |RunLoop| to avoid blocking
  // in the error case.
  void ResetSyncIfNoPendingError(SyncWait* sync);

  // Returns false if no error is pending. Otherwise, if the error is recoverable (e.g. a process
  // exit when not detecting exits), it recovers and returns false. Otherwise, it records the
  // |last_input|, determines its result, and returns true.
  bool HasError(const Input* last_input) FXL_LOCKS_EXCLUDED(mutex_);

  // Stop-related methods.
  void CloseImpl();
  void InterruptImpl();
  void JoinImpl();

  // General configuration.
  std::shared_ptr<Options> options_;
  uint32_t run_ = 0;
  zx::time start_ = zx::time::infinite_past();
  zx::time next_pulse_ = zx::time::infinite();

  // Variables to synchronize between the worker and run-loop.
  std::atomic<bool> stopped_ = true;
  std::atomic<bool> stopping_ = false;
  Input* next_input_ = nullptr;
  Input* last_input_ = nullptr;
  SyncWait next_input_ready_;
  SyncWait next_input_taken_;
  SyncWait last_input_ready_;
  SyncWait last_input_taken_;
  SyncWait run_finished_;

  // Timer variables
  std::thread timer_;
  SyncWait timer_sync_;
  zx::time run_deadline_ FXL_GUARDED_BY(mutex_) = zx::time::infinite();

  // Input generation and management variables.
  std::shared_ptr<Corpus> seed_corpus_;
  std::shared_ptr<Corpus> live_corpus_;
  Mutagen mutagen_;

  // Interfaces to other components.
  std::unique_ptr<TargetAdapterClient> target_adapter_;
  std::unique_ptr<CoverageProviderClient> coverage_provider_;

  // Feedback collection and analysis variables.
  std::shared_ptr<ModulePool> pool_;
  std::mutex mutex_;
  std::unordered_map<uint64_t, std::unique_ptr<ProcessProxyImpl>> process_proxies_
      FXL_GUARDED_BY(mutex_);
  std::atomic<size_t> pending_signals_ = 0;
  SyncWait process_sync_;

  // The target ID of the process that caused an error, or a value reserved for timeouts.
  std::atomic<uint64_t> error_ = kInvalidTargetId;

  RunOnce close_;
  RunOnce interrupt_;
  RunOnce join_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(RunnerImpl);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_ENGINE_RUNNER_H_
