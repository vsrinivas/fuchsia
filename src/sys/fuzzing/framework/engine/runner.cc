// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/runner.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <zircon/sanitizer.h>
#include <zircon/status.h>

#include <deque>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/framework/target/process.h"

namespace fuzzing {

using ::fuchsia::fuzzer::CoverageEvent;
using ::fuchsia::fuzzer::MAX_PROCESS_STATS;

RunnerImpl::RunnerImpl()
    : close_([this] { CloseImpl(); }),
      interrupt_([this]() { InterruptImpl(); }),
      join_([this]() { JoinImpl(); }) {
  timer_ = std::thread([this]() { Timer(); });
  seed_corpus_ = std::make_shared<Corpus>();
  live_corpus_ = std::make_shared<Corpus>();
  pool_ = std::make_shared<ModulePool>();
}

RunnerImpl::~RunnerImpl() {
  close_.Run();
  interrupt_.Run();
  join_.Run();
}

void RunnerImpl::AddDefaults(Options* options) {
  Corpus::AddDefaults(options);
  Mutagen::AddDefaults(options);
  ProcessProxyImpl::AddDefaults(options);
  TargetAdapterClient::AddDefaults(options);
  if (!options->has_runs()) {
    options->set_runs(kDefaultRuns);
  }
  if (!options->has_max_total_time()) {
    options->set_max_total_time(kDefaultMaxTotalTime);
  }
  if (!options->has_max_input_size()) {
    options->set_max_input_size(kDefaultMaxInputSize);
  }
  if (!options->has_mutation_depth()) {
    options->set_mutation_depth(kDefaultMutationDepth);
  }
  if (!options->has_detect_exits()) {
    options->set_detect_exits(kDefaultDetectExits);
  }
  if (!options->has_detect_leaks()) {
    options->set_detect_leaks(kDefaultDetectLeaks);
  }
  if (!options->has_run_limit()) {
    options->set_run_limit(kDefaultRunLimit);
  }
  if (!options->has_pulse_interval()) {
    options->set_pulse_interval(kDefaultPulseInterval);
  }
}

void RunnerImpl::ConfigureImpl(const std::shared_ptr<Options>& options) {
  options_ = options;
  seed_corpus_->Configure(options_);
  live_corpus_->Configure(options_);
  mutagen_.Configure(options_);
  if (target_adapter_) {
    target_adapter_->Configure(options_);
  }
  if (coverage_provider_) {
    coverage_provider_->Configure(options_);
  }
}

zx_status_t RunnerImpl::AddToCorpus(CorpusType corpus_type, Input input) {
  switch (corpus_type) {
    case CorpusType::SEED:
      seed_corpus_->Add(std::move(input));
      break;
    case CorpusType::LIVE:
      live_corpus_->Add(std::move(input));
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

Input RunnerImpl::ReadFromCorpus(CorpusType corpus_type, size_t offset) {
  Input input;
  switch (corpus_type) {
    case CorpusType::SEED:
      seed_corpus_->At(offset, &input);
      break;
    case CorpusType::LIVE:
      live_corpus_->At(offset, &input);
      break;
    default:
      FX_NOTREACHED();
  }
  return input;
}

zx_status_t RunnerImpl::ParseDictionary(const Input& input) {
  Dictionary dict;
  dict.Configure(options_);
  if (!dict.Parse(input)) {
    return ZX_ERR_INVALID_ARGS;
  }
  mutagen_.set_dictionary(std::move(dict));
  return ZX_OK;
}

Input RunnerImpl::GetDictionaryAsInput() const { return mutagen_.dictionary().AsInput(); }

///////////////////////////////////////////////////////////////
// Synchronous workflows.

zx_status_t RunnerImpl::SyncExecute(const Input& input) {
  auto scope = SyncScope();
  TestOne(input);
  return ZX_OK;
}

zx_status_t RunnerImpl::SyncMinimize(const Input& input) {
  auto scope = SyncScope();
  TestOne(input);
  if (result() == FuzzResult::NO_ERRORS) {
    FX_LOGS(WARNING) << "Test input did not trigger an error.";
    return ZX_ERR_INVALID_ARGS;
  }
  auto minimized = result_input();
  auto saved_result = result();
  auto saved_corpus = live_corpus_;
  auto saved_options = CopyOptions(*options_);
  if (!options_->has_runs() && !options_->has_max_total_time()) {
    FX_LOGS(INFO) << "'max_total_time' and 'runs' are both not set. Defaulting to 10 minutes.";
    options_->set_max_total_time(zx::min(10).get());
  }
  while (true) {
    if (minimized.size() < 2) {
      FX_LOGS(INFO) << "Input is " << minimized.size() << " byte(s); will not minimize further.";
      break;
    }
    auto max_size = minimized.size() - 1;
    auto next_input = minimized.Duplicate();
    next_input.Truncate(max_size);
    options_->set_max_input_size(max_size);
    pool_->Clear();
    live_corpus_ = std::make_shared<Corpus>();
    live_corpus_->Configure(options_);
    auto status = live_corpus_->Add(std::move(next_input));
    FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
    // Imitate libFuzzer and count from 0 so long as errors are found.
    ClearErrors();
    run_ = 0;
    FuzzLoop();
    if (result() == FuzzResult::NO_ERRORS) {
      FX_LOGS(INFO) << "Did not reduce error input beyond " << minimized.size()
                    << " bytes; exiting.";
      break;
    }
    // TODO(fxbug.dev/85424): This needs a more rigorous way of deduplicating crashes.
    if (result() != saved_result) {
      FX_LOGS(WARNING) << "Different error detected; will not minimize further.";
      break;
    }
    minimized = result_input();
  }
  set_result_input(minimized);
  pool_->Clear();
  live_corpus_ = saved_corpus;
  *options_ = std::move(saved_options);
  return ZX_OK;
}

zx_status_t RunnerImpl::SyncCleanse(const Input& input) {
  auto scope = SyncScope();
  auto cleansed = input.Duplicate();
  const std::vector<uint8_t> kClean = {' ', 0xff};
  auto clean = kClean.begin();
  std::deque<size_t> offsets;
  for (size_t i = 0; i < input.size(); ++i) {
    // Record which offsets are replaceable.
    if (std::find(kClean.begin(), kClean.end(), input.data()[i]) == kClean.end()) {
      offsets.push_back(i);
    }
  }
  size_t left = offsets.size();
  constexpr size_t kMaxCleanseAttempts = 5;
  size_t tries = kMaxCleanseAttempts;
  uint8_t orig = 0;
  bool mod = false;
  // Try various bytes at various offsets. To match existing engines (i.e. libFuzzer), this code
  // does not distinguish between different types of errors.
  FuzzLoopStrict(
      /* next_input */
      [&cleansed, &clean, &kClean, &offsets, &left, &tries, &mod, &orig](bool first) -> Input* {
        auto* data = cleansed.data();
        if (clean == kClean.end()) {
          clean = kClean.begin();
          offsets.push_back(offsets.front());
          offsets.pop_front();
          --left;
        }
        if (!left) {
          left = offsets.size();
          tries = mod ? (tries - 1) : 0;
          mod = false;
        }
        if (tries == 0) {
          return nullptr;
        }
        auto offset = offsets.front();
        orig = data[offset];
        data[offset] = *clean;
        return &cleansed;
      },
      /* finish_run */
      [this, &cleansed, &clean, &kClean, &offsets, &left, &mod, &orig](Input* ignored) {
        auto* data = cleansed.data();
        if (result() != FuzzResult::NO_ERRORS) {
          ClearErrors();
          clean = kClean.begin();
          offsets.pop_front();
          --left;
          mod = true;
        } else {
          data[offsets.front()] = orig;
          ++clean;
        }
      },
      /* ignore_errors */ true);
  set_result_input(cleansed);
  return ZX_OK;
}

zx_status_t RunnerImpl::SyncFuzz() {
  auto scope = SyncScope();
  pool_->Clear();
  // Add seed corpus to live corpus.
  for (size_t offset = 0; offset < seed_corpus_->num_inputs(); ++offset) {
    Input input;
    seed_corpus_->At(offset, &input);
    live_corpus_->Add(std::move(input));
  }
  TestCorpus(live_corpus_);
  FuzzLoop();
  return ZX_OK;
}

zx_status_t RunnerImpl::SyncMerge() {
  auto scope = SyncScope();
  // Measure the coverage of the seed corpus.
  pool_->Clear();
  // TODO(fxbug.dev/84364): |FuzzLoopRelaxed| is preferred here and elsewhere in this function, but
  // using that causes some test flake. Switch to that version once the source of it is resolved.
  size_t offset = 0;
  Input input;
  TestCorpus(seed_corpus_);
  if (result() != FuzzResult::NO_ERRORS) {
    FX_LOGS(WARNING) << "Seed corpus input triggered an error.";
    return ZX_ERR_INVALID_ARGS;
  }

  // Measure the additional coverage of each input in the live corpus, and sort.
  std::vector<Input> error_inputs;
  std::vector<Input> inputs;
  offset = 0;
  FuzzLoopStrict(
      /* next_input */
      [this, &offset, &input](bool first) {
        return live_corpus_->At(offset++, &input) ? &input : nullptr;
      },
      /* finish_run */
      [this, &error_inputs, &inputs](Input* last_input) {
        if (result() != FuzzResult::NO_ERRORS) {
          FX_LOGS(WARNING) << "Corpus contains an input that triggers an error.";
          error_inputs.push_back(last_input->Duplicate());
          ClearErrors();
          return;
        }
        auto num_features = pool_->Measure();
        if (last_input->size() && num_features) {
          last_input->set_num_features(num_features);
          inputs.push_back(std::move(*last_input));
        }
      },
      /* ignore_errors */ true);
  std::sort(inputs.begin(), inputs.end());

  // Keep files that add coverage.
  live_corpus_ = std::make_shared<Corpus>();
  live_corpus_->Configure(options_);
  auto iter = inputs.begin();
  FuzzLoopStrict(
      /* next_input */ [&iter, &inputs](
                           bool first) { return iter == inputs.end() ? nullptr : &(*iter++); },
      /* finish_run*/
      [this](Input* last_input) {
        size_t unique_features = pool_->Accumulate();
        if (result() != FuzzResult::NO_ERRORS || unique_features) {
          auto status = live_corpus_->Add(std::move(*last_input));
          FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
        }
      },
      /* ignore_errors */ true);

  // Always preserve error inputs.
  for (auto& input : error_inputs) {
    live_corpus_->Add(std::move(input));
  }
  return ZX_OK;
}

///////////////////////////////////////////////////////////////
// Run-related methods.

void RunnerImpl::TestOne(const Input& input) {
  auto dup = input.Duplicate();
  FuzzLoopStrict(/* next_input */ [&dup](bool first) { return first ? &dup : nullptr; },
                 /* finish_run */ [](Input* last_input) {});
}

void RunnerImpl::TestCorpus(const std::shared_ptr<Corpus>& corpus) {
  size_t offset = 0;
  Input input;
  FuzzLoopStrict(
      /* next_input */
      [corpus, &offset, &input](bool first) {
        return corpus->At(offset++, &input) ? &input : nullptr;
      },
      /* finish_run */ [this](const Input* last_input) { pool_->Accumulate(); });
}

void RunnerImpl::FuzzLoop() {
  // Use two pre-allocated inputs, and swap the pointers between them each iteration, i.e. the old
  // |next_input| becomes |prev_input|, and the old |prev_input| is recycled to a new |next_input|.
  Input inputs[2];
  auto* next_input = &inputs[0];
  auto* prev_input = &inputs[1];
  next_input->Reserve(options_->max_input_size());
  prev_input->Reserve(options_->max_input_size());
  auto max_time = zx::duration(options_->max_total_time());
  auto deadline = max_time.get() ? zx::deadline_after(max_time) : zx::time::infinite();
  auto runs = options_->runs();
  // TODO(fxbug.dev/84364): |FuzzLoopRelaxed| is preferred here, but using that causes some test
  // flake. Switch to that version once the source of it is resolved.
  FuzzLoopStrict(/* next_input */
                 [this, deadline, runs, &next_input, &prev_input](bool first) -> Input* {
                   std::swap(next_input, prev_input);
                   if (stopping_ || (zx::clock::get_monotonic() >= deadline) ||
                       (runs != 0 && run_ >= runs)) {
                     return nullptr;
                   }
                   // Change the input after |options_->mutation_depth()| mutations. Doing so
                   // resets the recorded sequence of mutations.
                   if (first || mutagen_.mutations().size() == options_->mutation_depth()) {
                     mutagen_.reset_mutations();
                     live_corpus_->Pick(mutagen_.base_input());
                     live_corpus_->Pick(mutagen_.crossover());
                   }
                   mutagen_.Mutate(next_input);
                   return next_input;
                 },
                 /* finish_run */
                 [this](Input* last_input) {
                   if (pool_->Accumulate()) {
                     live_corpus_->Add(last_input->Duplicate());
                     UpdateMonitors(UpdateReason::NEW);
                   } else if (zx::clock::get_monotonic() >= next_pulse_) {
                     UpdateMonitors(UpdateReason::PULSE);
                     next_pulse_ = zx::deadline_after(zx::sec(options_->pulse_interval()));
                   }
                 });
}

void RunnerImpl::FuzzLoopStrict(fit::function<Input*(bool)> next_input,
                                fit::function<void(Input*)> finish_run, bool ignore_errors) {
  next_input_ = next_input(/* first */ true);
  last_input_ = nullptr;
  // Set initial sync state.
  next_input_ready_.Signal();
  next_input_taken_.Reset();
  last_input_taken_.Signal();
  last_input_ready_.Reset();
  auto loop = std::thread([this, ignore_errors]() { RunLoop(ignore_errors); });
  while (true) {
    last_input_ready_.WaitFor("feedback from last input");
    last_input_ready_.Reset();
    if (stopped_) {
      break;
    }
    // Analyze feedback from inputN
    auto* last_input = last_input_;
    last_input_taken_.Signal();
    finish_run(last_input);
    next_input_taken_.WaitFor("next input to be consumed");
    next_input_taken_.Reset();
    if (stopped_) {
      break;
    }
    // Generate inputN+1
    next_input_ = next_input(/* first */ false);
    next_input_ready_.Signal();
  }
  last_input_taken_.Signal();
  next_input_ready_.Signal();
  loop.join();
}

void RunnerImpl::FuzzLoopRelaxed(fit::function<Input*(bool)> next_input,
                                 fit::function<void(Input*)> finish_run, bool ignore_errors) {
  next_input_ = next_input(/* first */ true);
  last_input_ = nullptr;
  next_input_ready_.Signal();
  next_input_taken_.Reset();
  last_input_taken_.Signal();
  last_input_ready_.Reset();
  auto loop = std::thread([this, ignore_errors]() { RunLoop(ignore_errors); });
  while (true) {
    next_input_taken_.WaitFor("next input to be consumed");
    next_input_taken_.Reset();
    if (stopped_) {
      break;
    }
    // Generate inputN+1
    next_input_ = next_input(/* first */ false);
    next_input_ready_.Signal();
    last_input_ready_.WaitFor("feedback to analyze");
    last_input_ready_.Reset();
    if (stopped_) {
      break;
    }
    // Analyze feedback from inputN
    auto* last_input = last_input_;
    last_input_taken_.Signal();
    finish_run(last_input);
  }
  last_input_taken_.Signal();
  next_input_ready_.Signal();
  loop.join();
}

void RunnerImpl::RunLoop(bool ignore_errors) {
  FX_CHECK(target_adapter_);
  // Leak detection is expensive, so the strategy is as follows:
  // 1. Try inputs once without leak detection.
  // 2. If leak detection is requested, check if leaks are suspected (unbalanced malloc/frees).
  // 3. If a leak if suspected, do the normal feedback analysis and then try the input again, this
  //    time with leak detection. Skip the feedback analysis on the second try.
  // 4. Keep track of how many suspected leaks don't result in an error. After
  //    |kMaxLeakDetectionAttempts|, disable further leak detection.
  constexpr size_t kMaxLeakDetectionAttempts = 1000;
  bool detect_leaks = false;
  size_t leak_detection_attempts = options_->detect_leaks() ? kMaxLeakDetectionAttempts : 0;

  Input* test_input = nullptr;
  stopped_ = false;
  while (!stopped_) {
    bool has_error = false;
    // Signal proxies that a run is about to begin.
    {
      std::lock_guard<std::mutex> lock(mutex_);
      pending_signals_ = process_proxies_.size();
      ResetSyncIfNoPendingError(&process_sync_);
      for (auto& process_proxy : process_proxies_) {
        process_proxy.second->Start(detect_leaks);
      }
    }
    // Wait for the next input to be ready. If attempting to detect a leak, use the previous input.
    if (!detect_leaks) {
      next_input_ready_.WaitFor("next input to be produced");
      next_input_ready_.Reset();
      // Get the next input, if there is one.
      test_input = next_input_;
      if (test_input) {
        next_input_taken_.Signal();
      }
    }
    // Wait for proxies to respond.
    while (pending_signals_ != 0) {
      process_sync_.WaitFor("processes to acknowledge start");
      has_error |= HasError(test_input);
    }
    if (has_error && !ignore_errors) {
      // Encounting an error before this point suggests the individual fuzzer may be
      // non-deterministic and/or non-hermetic and should be improved.
      FX_LOGS(WARNING) << "Detected error between fuzzing runs.";
      break;
    }
    // Start the fuzzing run by telling the target adapter that the test input is ready.
    if (!test_input) {
      break;
    }
    ++run_;
    target_adapter_->Start(test_input);
    ResetTimer();
    // Wait for the adapter to signal the run is complete.
    target_adapter_->AwaitFinish();
    has_error = HasError(test_input);
    // Signal proxies that a run has ended.
    {
      std::lock_guard<std::mutex> lock(mutex_);
      pending_signals_ = process_proxies_.size();
      ResetSyncIfNoPendingError(&process_sync_);
      for (auto& process_proxy : process_proxies_) {
        process_proxy.second->Finish();
      }
    }
    // Wait for proxies to respond.
    while (pending_signals_ != 0) {
      process_sync_.WaitFor("processes to acknowledge finish");
      has_error |= HasError(test_input);
    }
    if (has_error && !ignore_errors) {
      break;
    }
    if (detect_leaks) {
      // This is a second try, with leak detection.
      --leak_detection_attempts;
      if (leak_detection_attempts == 0) {
        FX_LOGS(INFO) << "Disabling leak detection: No memory leaks were found in any of "
                      << kMaxLeakDetectionAttempts << " inputs suspected of leaking. "
                      << "Memory may be accumulating in some global state without leaking. "
                      << "End-of-process leak checks will still be performed.";
      }
      detect_leaks = false;
      // Skip feedback analysis; this was already done on the first try.
      continue;
    }
    if (leak_detection_attempts && !detect_leaks) {
      // This is a first try, and leak detection is requested.
      std::lock_guard<std::mutex> lock(mutex_);
      for (auto& process_proxy : process_proxies_) {
        detect_leaks |= process_proxy.second->leak_suspected();
      }
    }
    // Inform the worker that it can analyze the feedback from last input now.
    last_input_taken_.WaitFor("feedback to be analyzed");
    last_input_taken_.Reset();
    last_input_ = test_input;
    last_input_ready_.Signal();
  }
  stopped_ = true;
  next_input_taken_.Signal();
  last_input_ready_.Signal();
}

void RunnerImpl::ClearErrors() {
  Runner::ClearErrors();
  error_ = 0;
  target_adapter_->ClearError();
  process_sync_.Reset();
}

///////////////////////////////////////////////////////////////
// Methods for,connecting to other components.

void RunnerImpl::SetTargetAdapter(std::unique_ptr<TargetAdapterClient> target_adapter) {
  FX_CHECK(options_);
  FX_CHECK(!target_adapter_);
  target_adapter_ = std::move(target_adapter);
  target_adapter_->Configure(options_);
  auto parameters = target_adapter_->GetParameters();
  std::vector<std::string> seed_corpus_dirs;
  std::copy_if(
      parameters.begin(), parameters.end(), std::back_inserter(seed_corpus_dirs),
      [](const std::string& parameter) { return !parameter.empty() && parameter[0] != '-'; });
  seed_corpus_->Load(seed_corpus_dirs);
}

void RunnerImpl::SetCoverageProvider(std::unique_ptr<CoverageProviderClient> coverage_provider) {
  FX_CHECK(options_);
  FX_CHECK(!coverage_provider_);
  coverage_provider_ = std::move(coverage_provider);
  coverage_provider_->Configure(options_);
  coverage_provider_->OnEvent([this](CoverageEvent event) {
    auto target_id = event.target_id;
    if (target_id == kInvalidTargetId || target_id == kTimeoutTargetId) {
      FX_LOGS(ERROR) << "CoverageEvent with invalid target_id: " << target_id;
      return;
    }
    auto payload = std::move(event.payload);
    if (payload.is_process_started()) {
      auto instrumented = std::move(payload.process_started());
      auto process_proxy = std::make_unique<ProcessProxyImpl>(target_id, pool_);
      process_proxy->Configure(options_);
      process_proxy->SetHandlers(
          /* signal_handler */ [this]() { OnSignal(); },
          /* error_handler */ [this](uint64_t target_id) { OnError(target_id); });
      {
        std::lock_guard<std::mutex> lock(mutex_);
        // This needs to be within the lock, since |OnError| may called as soon as |Connect| is
        // called, and it will expect to find the |target_id| in |process_proxies_|.
        process_proxy->Connect(std::move(instrumented));
        process_proxies_[target_id] = std::move(process_proxy);
      }
    }
    if (payload.is_llvm_module_added()) {
      auto llvm_module = std::move(payload.llvm_module_added());
      {
        std::lock_guard<std::mutex> lock(mutex_);
        auto process_proxy = process_proxies_.find(target_id);
        if (process_proxy == process_proxies_.end()) {
          FX_LOGS(WARNING) << "CoverageEvent.LlvmModuleAdded: no such target_id: " << target_id;
        } else {
          process_proxy->second->AddLlvmModule(std::move(llvm_module));
        }
      }
    }
  });
}

///////////////////////////////////////////////////////////////
// Signalling-related methods.

bool RunnerImpl::OnSignal() {
  // "Normal" signals are received in response to signals sent to start or finish a run. |RunLoop|
  // keeps track of how many of these signals are sent using |pending_signals_|.
  auto pending = pending_signals_.fetch_sub(1);
  FX_DCHECK(pending);
  if (pending == 1) {
    process_sync_.Signal();
  }
  return true;
}

void RunnerImpl::OnError(uint64_t error) {
  // Only the first process_proxy to detect an error awakens the |RunLoop|. Subsequent errors are
  // dropped.
  uint64_t expected = 0;
  if (error_.compare_exchange_strong(expected, error)) {
    target_adapter_->SetError();
    process_sync_.Signal();
  }
}

void RunnerImpl::ResetSyncIfNoPendingError(SyncWait* sync) {
  // Avoid race by resetting then "unresetting", i.e. signalling, if there's a pending error.
  sync->Reset();
  if (error_.load()) {
    sync->Signal();
  }
}

bool RunnerImpl::HasError(const Input* last_input) {
  auto error = error_.load();
  if (error == kInvalidTargetId) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (error != kTimeoutTargetId) {
    // Almost every error causes the process to exit...
    auto process_proxy = process_proxies_.find(error);
    FX_CHECK(process_proxy != process_proxies_.end())
        << "Received error from unknown target_id: " << error;
    set_result(process_proxy->second->GetResult());
  } else {
    /// .. except for timeouts.
    set_result(FuzzResult::TIMEOUT);
    constexpr size_t kBufSize = 1ULL << 20;
    auto buf = std::make_unique<char[]>(kBufSize);
    for (auto& process_proxy : process_proxies_) {
      auto len = process_proxy.second->Dump(buf.get(), kBufSize);
      __sanitizer_log_write(buf.get(), len);
    }
  }
  // If it's an ignored exit(),just remove that one process_proxy and treat it like a signal.
  if (result() == FuzzResult::EXIT && !options_->detect_exits()) {
    process_proxies_.erase(error);
    ClearErrors();
    if (pending_signals_) {
      OnSignal();
    }
    return false;
  }
  // Otherwise, it's really an error. Remove the target adapter and all proxies.
  target_adapter_->Close();
  process_proxies_.clear();
  if (last_input) {
    set_result_input(*last_input);
  }
  error_ = 0;
  return true;
}

///////////////////////////////////////////////////////////////
// Timer methods. See also |SyncScope| below.

void RunnerImpl::ResetTimer() {
  auto run_limit = zx::duration(options_->run_limit());
  {
    std::lock_guard<std::mutex> lock(mutex_);
    run_deadline_ = run_limit.get() ? zx::deadline_after(run_limit) : zx::time::infinite();
    timer_sync_.Signal();
  }
}

void RunnerImpl::Timer() {
  while (true) {
    zx::time run_deadline;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      run_deadline = run_deadline_;
    }
    if (run_deadline == zx::time::infinite_past()) {
      break;
    }
    if (run_deadline < zx::clock::get_monotonic()) {
      OnError(kTimeoutTargetId);
      timer_sync_.WaitFor("error to be handled");
    } else {
      timer_sync_.WaitUntil(run_deadline);
    }
    timer_sync_.Reset();
  }
}

///////////////////////////////////////////////////////////////
// Status-related methods.

fit::deferred_action<fit::closure> RunnerImpl::SyncScope() {
  ClearErrors();
  run_ = 0;
  start_ = zx::clock::get_monotonic();
  ResetTimer();
  stopped_ = false;
  UpdateMonitors(UpdateReason::INIT);
  return fit::defer<fit::closure>([this]() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      run_deadline_ = zx::time::infinite();
      timer_sync_.Signal();
    }
    stopped_ = true;
    UpdateMonitors(UpdateReason::DONE);
  });
}

Status RunnerImpl::CollectStatus() {
  Status status;
  status.set_running(!stopped_);
  status.set_runs(run_);

  auto elapsed = zx::clock::get_monotonic() - start_;
  status.set_elapsed(elapsed.to_nsecs());

  size_t covered_features;
  auto covered_pcs = pool_->GetCoverage(&covered_features);
  status.set_covered_pcs(covered_pcs);
  status.set_covered_features(covered_features);

  status.set_corpus_num_inputs(seed_corpus_->num_inputs() + live_corpus_->num_inputs());
  status.set_corpus_total_size(seed_corpus_->total_size() + live_corpus_->total_size());

  std::vector<ProcessStats> all_stats;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    all_stats.reserve(std::min<size_t>(process_proxies_.size(), MAX_PROCESS_STATS));
    for (auto& process_proxy : process_proxies_) {
      if (all_stats.size() == all_stats.capacity()) {
        break;
      }
      ProcessStats stats;
      auto status = process_proxy.second->GetStats(&stats);
      if (status == ZX_OK) {
        all_stats.push_back(stats);
      } else {
        FX_LOGS(WARNING) << "Failed to get stats for process: " << zx_status_get_string(status);
      }
    }
  }
  status.set_process_stats(std::move(all_stats));

  return status;
}

///////////////////////////////////////////////////////////////
// Stop-related methods.

void RunnerImpl::CloseImpl() { Runner::Close(); }

void RunnerImpl::InterruptImpl() {
  Runner::Interrupt();
  stopping_ = true;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    run_deadline_ = zx::time::infinite_past();
    timer_sync_.Signal();
  }
}

void RunnerImpl::JoinImpl() {
  if (timer_.joinable()) {
    timer_.join();
  }
  Runner::Join();
}

}  // namespace fuzzing
