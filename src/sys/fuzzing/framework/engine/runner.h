// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_FRAMEWORK_ENGINE_RUNNER_H_
#define SRC_SYS_FUZZING_FRAMEWORK_ENGINE_RUNNER_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/function.h>
#include <lib/sync/completion.h>
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
#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/runner.h"
#include "src/sys/fuzzing/common/shared-memory.h"
#include "src/sys/fuzzing/common/signal-coordinator.h"
#include "src/sys/fuzzing/framework/engine/corpus.h"
#include "src/sys/fuzzing/framework/engine/module-pool.h"
#include "src/sys/fuzzing/framework/engine/mutagen.h"
#include "src/sys/fuzzing/framework/engine/process-proxy.h"

namespace fuzzing {

using ::fuchsia::fuzzer::MonitorPtr;
using ::fuchsia::fuzzer::Result;
using ::fuchsia::fuzzer::Status;
using ::fuchsia::fuzzer::TargetAdapter;
using ::fuchsia::fuzzer::TargetAdapterSyncPtr;
using ::fuchsia::fuzzer::UpdateReason;
using CorpusType = ::fuchsia::fuzzer::Corpus;

// Represents the different types of fuzzing-ending errors. The runner below includes an atomic
// pointer to an instance of this struct. If an input results in multiple errors from different
// processes, only the first assignment to that pointer is handled as the primary error.
struct Error {
  ProcessProxyImpl* exited = nullptr;
  bool timeout = false;

  explicit Error(ProcessProxyImpl* source) : exited(source) {}
  Error() : timeout(true){};
  ~Error() = default;
};

// The concrete implementation of |Runner|.
class RunnerImpl final : public Runner {
 public:
  RunnerImpl();
  ~RunnerImpl() override;

  // |Runner| method implementations.
  void AddDefaults(Options* options) override;
  zx_status_t AddToCorpus(CorpusType corpus_type, Input input) override;
  Input ReadFromCorpus(CorpusType corpus_type, size_t offset) const override;
  zx_status_t ParseDictionary(const Input& input) override;
  Input GetDictionaryAsInput() const override;

  // Configure where to send |TargetAdapter| requests.
  void SetTargetAdapterHandler(fidl::InterfaceRequestHandler<TargetAdapter> handler);

  // Handle incoming |ProcessProxy| requests.
  fidl::InterfaceRequestHandler<ProcessProxy> GetProcessProxyHandler(
      async_dispatcher_t* dispatcher);

  // Callbacks for signals received from the target adapter and process proxies that are used to
  // notify the runner that they have started, finished, or encountered an error.
  bool OnSignal();
  void OnError(std::unique_ptr<Error> error);

 protected:
  // Fuzzing workflow implementations.
  void ConfigureImpl(const std::shared_ptr<Options>& options) override;
  zx_status_t SyncExecute(const Input& input) override;
  zx_status_t SyncMinimize(const Input& input) override;
  zx_status_t SyncCleanse(const Input& input) override;
  zx_status_t SyncFuzz() override;
  zx_status_t SyncMerge() override;

  Status CollectStatusLocked() override FXL_REQUIRE(mutex_);

 private:
  // Configure both the run and overall deadlines, and sets an alarm for the run deadline on the
  // timer thread. When it expires, a TIMEOUT error is triggered.
  void StartTimer();

  // Resets the timer thread's alarm, setting a new run deadline and delaying how long until the
  // TIMEOUT error is triggered.
  void ResetTimer();

  // Clears the timer thread's alarm, removing the run deadline. No TIMEOUT errors will occur unless
  // |StartTimer| or |ResetTimer| are called again.
  void StopTimer();

  // The timer thread body.
  void Timer();

  // Sends the test input to the target adapter.
  void TestOne(const Input& input);

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
  void RunLoop(bool ignore_errors);

  // Wraps |FuzzLoopStrict| to perform "normal" fuzzing, i.e. mutates an input from the live corpus,
  // accumulates feedback from each input, and exits on error, max runs, or max time.
  //
  // TODO(fxbug.dev/84364): |FuzzLoopRelaxed| is preferred here, but using that causes some test
  // flake. Switch to that version once the source of it is resolved.
  void FuzzLoop();

  // Resets the error state for subsequent actions.
  void ClearErrors();

  // Uses the target adapter request handler to connect to the target adapter.
  void ConnectTargetAdapter();

  // Resets |sync|, but only if there is no pending error, allowing |RunLoop| to avoid blocking
  // in the error case.
  void ResetSyncIfNoPendingError(sync_completion_t* sync);

  // Returns false if no error is pending. Otherwise, if the error is recoverable (e.g. a process
  // exit when not detecting exits), it recovers and returns false. Otherwise, it records the
  // |last_input|, determines its result, and returns true.
  bool HasError(const Input* last_input);

  // General configuration.
  std::shared_ptr<Options> options_;
  uint32_t run_ = 0;
  zx::time start_ = zx::time::infinite_past();
  zx::time deadline_ = zx::time::infinite();
  zx::time next_pulse_ = zx::time::infinite();

  // Variables to synchronize between the worker and run-loop.
  std::atomic<bool> stopped_ = true;
  Input* next_input_ = nullptr;
  Input* last_input_ = nullptr;
  sync_completion_t next_input_ready_;
  sync_completion_t next_input_taken_;
  sync_completion_t last_input_ready_;
  sync_completion_t last_input_taken_;
  sync_completion_t run_finished_;

  // Timer variables
  std::thread timer_;
  sync_completion_t timer_sync_;
  zx::time run_deadline_ = zx::time::infinite();

  // Input generation and management variables.
  std::shared_ptr<Corpus> seed_corpus_;
  std::shared_ptr<Corpus> live_corpus_;
  Mutagen mutagen_;

  // Variables used to send data to and coordinate with the target adapter.
  fidl::InterfaceRequestHandler<TargetAdapter> target_adapter_handler_;
  TargetAdapterSyncPtr target_adapter_;
  SignalCoordinator coordinator_;
  SharedMemory test_input_;
  sync_completion_t adapter_sync_;

  // Feedback collection and analysis variables.
  std::shared_ptr<ModulePool> pool_;
  std::mutex mutex_;
  std::vector<std::unique_ptr<ProcessProxyImpl>> proxies_ FXL_GUARDED_BY(mutex_);
  std::atomic<size_t> pending_proxy_signals_ = 0;
  sync_completion_t process_sync_;
  std::atomic<Error*> error_ = nullptr;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(RunnerImpl);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_ENGINE_RUNNER_H_
