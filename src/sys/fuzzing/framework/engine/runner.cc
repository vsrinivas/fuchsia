// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/runner.h"

#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <zircon/sanitizer.h>
#include <zircon/status.h>

#include "src/lib/fxl/macros.h"

namespace fuzzing {
namespace {

using ::fuchsia::fuzzer::MAX_PROCESS_STATS;

// This struct can be used with |std::sort| to sort inputs according to smallest first, then most
// features.
struct InputComparator {
  inline bool operator()(const Input& input1, const Input& input2) {
    return (input1.size() < input2.size()) ||
           (input1.size() == input2.size() && (input1.num_features() > input2.num_features()));
  }
};

}  // namespace

RunnerImpl::RunnerImpl() {
  timer_ = std::thread([this]() { Timer(); });
  seed_corpus_ = std::make_shared<Corpus>();
  live_corpus_ = std::make_shared<Corpus>();
  pool_ = std::make_shared<ModulePool>();
}

RunnerImpl::~RunnerImpl() {
  run_deadline_ = zx::time::infinite_past();
  sync_completion_signal(&timer_sync_);
  timer_.join();
}

void RunnerImpl::ConfigureImpl(const std::shared_ptr<Options>& options) {
  options_ = options;
  seed_corpus_->Configure(options_);
  live_corpus_->Configure(options_);
  mutagen_.Configure(options_);
  test_input_.Reserve(options_->max_input_size());
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

Input RunnerImpl::ReadFromCorpus(CorpusType corpus_type, size_t offset) const {
  Input* input = nullptr;
  switch (corpus_type) {
    case CorpusType::SEED:
      input = seed_corpus_->At(offset);
      break;
    case CorpusType::LIVE:
      input = live_corpus_->At(offset);
      break;
    default:
      FX_NOTREACHED();
  }
  return input ? input->Duplicate() : Input();
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
// Timer methods.

void RunnerImpl::StartTimer() {
  auto max_total_time = zx::duration(options_->max_total_time());
  start_ = zx::clock::get_monotonic();
  deadline_ = max_total_time.get() ? zx::deadline_after(max_total_time) : zx::time::infinite();
  ResetTimer();
}

void RunnerImpl::ResetTimer() {
  auto run_limit = zx::duration(options_->run_limit());
  run_deadline_ = run_limit.get() ? zx::deadline_after(run_limit) : zx::time::infinite();
  sync_completion_signal(&timer_sync_);
}

void RunnerImpl::StopTimer() {
  run_deadline_ = zx::time::infinite();
  sync_completion_signal(&timer_sync_);
}

void RunnerImpl::Timer() {
  while (run_deadline_ != zx::time::infinite_past()) {
    if (run_deadline_ < zx::clock::get_monotonic()) {
      OnError(std::make_unique<Error>());
      sync_completion_wait(&timer_sync_, ZX_TIME_INFINITE);
    } else {
      sync_completion_wait_deadline(&timer_sync_, run_deadline_.get());
    }
    sync_completion_reset(&timer_sync_);
  }
}

///////////////////////////////////////////////////////////////
// Synchronous workflows.

zx_status_t RunnerImpl::SyncExecute(const Input& input) {
  run_ = 0;
  TestOne(input);
  return ZX_OK;
}

zx_status_t RunnerImpl::SyncMinimize(const Input& input) {
  run_ = 0;
  TestOne(input);
  if (result() == Result::NO_ERRORS) {
    FX_LOGS(WARNING) << "Test input did not trigger an error.";
    return ZX_ERR_INVALID_ARGS;
  }
  auto minimized = result_input();
  if (minimized.size() == 0) {
    FX_LOGS(INFO) << "Empty input is already minimized.";
    return ZX_OK;
  }
  auto saved_result = result();
  auto saved_corpus = live_corpus_;
  auto saved_options = CopyOptions(*options_);
  if (!options_->has_runs() && !options_->has_max_total_time()) {
    FX_LOGS(INFO) << "'max_total_time' and 'runs' are both not set. Defaulting to 10 minutes.";
    options_->set_max_total_time(zx::min(10).get());
  }
  UpdateMonitors(UpdateReason::INIT);
  while (minimized.size() != 0) {
    auto max_size = minimized.size() - 1;
    auto next_input = minimized.Duplicate();
    next_input.Truncate(max_size);
    options_->set_max_input_size(max_size);
    pool_->Clear();
    live_corpus_ = std::make_shared<Corpus>();
    live_corpus_->Configure(options_);
    auto status = live_corpus_->Add(std::move(next_input));
    FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
    FuzzLoop();
    if (result() == Result::NO_ERRORS) {
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
    FX_LOGS(INFO) << "Reduced error input to " << minimized.size() << " bytes"
                  << (minimized.size() > 1 ? "; continuing..." : ".");
  }
  UpdateMonitors(UpdateReason::DONE);
  set_result_input(minimized);
  pool_->Clear();
  live_corpus_ = saved_corpus;
  *options_ = std::move(saved_options);
  return ZX_OK;
}

zx_status_t RunnerImpl::SyncCleanse(const Input& input) {
  run_ = 0;
  TestOne(input);
  if (result() == Result::NO_ERRORS) {
    FX_LOGS(WARNING) << "Test input did not trigger an error.";
    return ZX_ERR_INVALID_ARGS;
  }
  auto cleansed = result_input();
  if (cleansed.size() == 0) {
    FX_LOGS(INFO) << "Empty input is already clean.";
    return ZX_OK;
  }
  auto saved_result = result();

  static const uint8_t kReplacements[] = {' ', 0xff};
  constexpr size_t kMaxCleanseAttempts = 5;
  size_t i = 0;
  bool modified = true;
  uint8_t original = 0;
  FuzzLoopStrict(
      /* next_input */
      [&cleansed, &i, &modified, &original](bool first) -> Input* {
        auto* data = cleansed.data();
        auto size = cleansed.size();
        while (true) {
          size_t replacement = i % sizeof(kReplacements);
          size_t offset = (i / sizeof(kReplacements)) % size;
          size_t attempt = i / (sizeof(kReplacements) * size);
          // Cap the number of the attempts.
          if (attempt == kMaxCleanseAttempts) {
            return nullptr;
          }
          // Only start a new attempt if still making progress..
          if (offset == 0 && replacement == 0) {
            if (!modified) {
              return nullptr;
            }
            modified = false;
          }
          original = data[offset];
          // For each new byte, check if it matches a replacement.
          bool replaceable = true;
          if (replacement == 0) {
            for (auto r : kReplacements) {
              if (r == original) {
                replaceable = false;
                break;
              }
            }
          }
          if (replaceable) {
            data[offset] = kReplacements[replacement];
            return &cleansed;
          }
          // Skip to the next byte.
          i += sizeof(kReplacements);
        }
      },
      /* finish_run */
      [this, &cleansed, saved_result, &i, &modified, &original](Input* last_input) {
        auto* data = cleansed.data();
        auto size = cleansed.size();
        if (result() == saved_result) {
          // Keep the replacement and advance to the next byte.
          size_t replacement = i % sizeof(kReplacements);
          i += sizeof(kReplacements) - replacement;
          modified = true;
        } else {
          size_t offset = (i / sizeof(kReplacements)) % size;
          // Restore the original byte and try the next replacement.
          data[offset] = original;
          ++i;
        }
        if (result() != Result::NO_ERRORS) {
          ClearErrors();
        }
      },
      /* ignore_errors */ true);
  set_result_input(cleansed);
  return ZX_OK;
}

zx_status_t RunnerImpl::SyncFuzz() {
  run_ = 0;
  pool_->Clear();
  UpdateMonitors(UpdateReason::INIT);
  // Add seed corpus to live and measure it.
  size_t offset = 0;
  Input* input;
  while ((input = seed_corpus_->At(offset++))) {
    live_corpus_->Add(input->Duplicate());
  }
  offset = 0;
  FuzzLoopStrict(
      /* next_input */ [this, &offset](bool first) { return live_corpus_->At(offset++); },
      /* finish_run */ [this](const Input* last_input) { pool_->Accumulate(); });
  FuzzLoop();
  UpdateMonitors(UpdateReason::DONE);
  return ZX_OK;
}

zx_status_t RunnerImpl::SyncMerge() {
  run_ = 0;
  // Measure the coverage of the seed corpus.
  pool_->Clear();
  // TODO(fxbug.dev/84364): |FuzzLoopRelaxed| is preferred here and elsewhere in this function, but
  // using that causes some test flake. Switch to that version once the source of it is resolved.
  size_t offset = 0;
  FuzzLoopStrict(
      /* next_input */ [this, &offset](bool first) { return seed_corpus_->At(offset++); },
      /* finish_run */ [this](const Input* last_input) { pool_->Accumulate(); });
  if (result() != Result::NO_ERRORS) {
    FX_LOGS(WARNING) << "Seed corpus input triggered an error.";
    return ZX_ERR_INVALID_ARGS;
  }

  // Measure the additional coverage of each input in the live corpus, and sort.
  std::vector<Input> error_inputs;
  std::vector<Input> inputs;
  offset = 0;
  FuzzLoopStrict(
      /* next_input */ [this, &offset](bool first) { return live_corpus_->At(offset++); },
      /* finish_run */
      [this, &error_inputs, &inputs](Input* last_input) {
        if (result() != Result::NO_ERRORS) {
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
  std::sort(inputs.begin(), inputs.end(), InputComparator());

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
        if (result() != Result::NO_ERRORS || unique_features) {
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

void RunnerImpl::FuzzLoop() {
  // Use two pre-allocated inputs, and swap the pointers between them each iteration, i.e. the old
  // |next_input| becomes |prev_input|, and the old |prev_input| is recycled to a new |next_input|.
  Input inputs[2];
  auto* next_input = &inputs[0];
  auto* prev_input = &inputs[1];
  next_input->Reserve(options_->max_input_size());
  prev_input->Reserve(options_->max_input_size());
  // TODO(fxbug.dev/84364): |FuzzLoopRelaxed| is preferred here, but using that causes some test
  // flake. Switch to that version once the source of it is resolved.
  FuzzLoopStrict(/* next_input */
                 [this, &next_input, &prev_input](bool first) -> Input* {
                   auto runs = options_->runs();
                   std::swap(next_input, prev_input);
                   if ((runs != 0 && run_ >= runs) || (zx::clock::get_monotonic() >= deadline_)) {
                     return nullptr;
                   }
                   // Change the input after |options_->mutation_depth()| mutations. Doing so
                   // resets the recorded sequence of mutations.
                   if (first || mutagen_.mutations().size() == options_->mutation_depth()) {
                     mutagen_.set_input(live_corpus_->Pick());
                     mutagen_.set_crossover(live_corpus_->Pick());
                   }
                   mutagen_.Mutate(next_input);
                   return next_input;
                 },
                 /* finish_run */
                 [this](Input* last_input) {
                   if (pool_->Accumulate()) {
                     live_corpus_->Add(std::move(*last_input));
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
  sync_completion_signal(&next_input_ready_);
  sync_completion_reset(&next_input_taken_);
  sync_completion_signal(&last_input_taken_);
  sync_completion_reset(&last_input_ready_);
  auto loop = std::thread([this, ignore_errors]() { RunLoop(ignore_errors); });
  while (true) {
    sync_completion_wait(&last_input_ready_, ZX_TIME_INFINITE);
    sync_completion_reset(&last_input_ready_);
    if (stopped_) {
      break;
    }
    // Analyze feedback from inputN
    auto* last_input = last_input_;
    sync_completion_signal(&last_input_taken_);
    finish_run(last_input);
    sync_completion_wait(&next_input_taken_, ZX_TIME_INFINITE);
    sync_completion_reset(&next_input_taken_);
    if (stopped_) {
      break;
    }
    // Generate inputN+1
    next_input_ = next_input(/* first */ false);
    sync_completion_signal(&next_input_ready_);
  }
  sync_completion_signal(&last_input_taken_);
  sync_completion_signal(&next_input_ready_);
  loop.join();
}

void RunnerImpl::FuzzLoopRelaxed(fit::function<Input*(bool)> next_input,
                                 fit::function<void(Input*)> finish_run, bool ignore_errors) {
  next_input_ = next_input(/* first */ true);
  last_input_ = nullptr;
  sync_completion_signal(&next_input_ready_);
  sync_completion_reset(&next_input_taken_);
  sync_completion_signal(&last_input_taken_);
  sync_completion_reset(&last_input_ready_);
  auto loop = std::thread([this, ignore_errors]() { RunLoop(ignore_errors); });
  while (true) {
    sync_completion_wait(&next_input_taken_, ZX_TIME_INFINITE);
    sync_completion_reset(&next_input_taken_);
    if (stopped_) {
      break;
    }
    // Generate inputN+1
    next_input_ = next_input(/* first */ false);
    sync_completion_signal(&next_input_ready_);
    sync_completion_wait(&last_input_ready_, ZX_TIME_INFINITE);
    sync_completion_reset(&last_input_ready_);
    if (stopped_) {
      break;
    }
    // Analyze feedback from inputN
    auto* last_input = last_input_;
    sync_completion_signal(&last_input_taken_);
    finish_run(last_input);
  }
  sync_completion_signal(&last_input_taken_);
  sync_completion_signal(&next_input_ready_);
  loop.join();
}

void RunnerImpl::RunLoop(bool ignore_errors) {
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
  ClearErrors();
  StartTimer();

  while (!stopped_) {
    bool has_error = false;
    // Signal proxies that a run is about to begin.
    {
      std::lock_guard<std::mutex> lock(mutex_);
      pending_proxy_signals_ = proxies_.size();
      ResetSyncIfNoPendingError(&process_sync_);
      for (auto& proxy : proxies_) {
        proxy->Start(detect_leaks);
      }
    }
    // Wait for the next input to be ready. If attempting to detect a leak, use the previous input.
    if (!detect_leaks) {
      sync_completion_wait(&next_input_ready_, ZX_TIME_INFINITE);
      sync_completion_reset(&next_input_ready_);
      // Get the next input, if there is one.
      test_input = next_input_;
      if (test_input) {
        sync_completion_signal(&next_input_taken_);
      }
    }
    // Wait for proxies to respond.
    while (pending_proxy_signals_ != 0) {
      sync_completion_wait(&process_sync_, ZX_TIME_INFINITE);
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
    test_input_.Clear();
    test_input_.Write(test_input->data(), test_input->size());
    ++run_;
    if (!coordinator_.is_valid()) {
      ConnectTargetAdapter();
    }
    ResetSyncIfNoPendingError(&adapter_sync_);
    coordinator_.SignalPeer(kStart);
    ResetTimer();
    // Wait for the adapter to signal the run is complete.
    sync_completion_wait(&adapter_sync_, ZX_TIME_INFINITE);
    has_error = HasError(test_input);
    // Signal proxies that a run has ended.
    {
      std::lock_guard<std::mutex> lock(mutex_);
      pending_proxy_signals_ = proxies_.size();
      ResetSyncIfNoPendingError(&process_sync_);
      for (auto& proxy : proxies_) {
        proxy->Finish();
      }
    }
    // Wait for proxies to respond.
    while (pending_proxy_signals_ != 0) {
      sync_completion_wait(&process_sync_, ZX_TIME_INFINITE);
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
      for (auto& proxy : proxies_) {
        detect_leaks |= proxy->leak_suspected();
      }
    }
    // Inform the worker that it can analyze the feedback from last input now.
    sync_completion_wait(&last_input_taken_, ZX_TIME_INFINITE);
    sync_completion_reset(&last_input_taken_);
    last_input_ = test_input;
    sync_completion_signal(&last_input_ready_);
  }
  StopTimer();
  stopped_ = true;
  sync_completion_signal(&next_input_taken_);
  sync_completion_signal(&last_input_ready_);
}

void RunnerImpl::ClearErrors() {
  set_result(Result::NO_ERRORS);
  set_result_input(Input());
  error_ = nullptr;
  sync_completion_reset(&adapter_sync_);
  sync_completion_reset(&process_sync_);
}

///////////////////////////////////////////////////////////////
// Signalling-related methods.

void RunnerImpl::SetTargetAdapterHandler(fidl::InterfaceRequestHandler<TargetAdapter> handler) {
  target_adapter_handler_ = std::move(handler);
  coordinator_.Reset();
}

fidl::InterfaceRequestHandler<ProcessProxy> RunnerImpl::GetProcessProxyHandler(
    async_dispatcher_t* dispatcher) {
  return [this, dispatcher](fidl::InterfaceRequest<ProcessProxy> request) {
    auto proxy = std::make_unique<ProcessProxyImpl>(pool_);
    proxy->Bind(std::move(request), dispatcher);
    proxy->Configure(options_);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      proxy->SetHandlers(
          [this]() { OnSignal(); },
          [this](ProcessProxyImpl* exited) { OnError(std::make_unique<Error>(exited)); });
      proxies_.push_back(std::move(proxy));
    }
  };
}

void RunnerImpl::ConnectTargetAdapter() {
  FX_DCHECK(target_adapter_handler_);
  target_adapter_handler_(target_adapter_.NewRequest());
  auto eventpair = coordinator_.Create([this](zx_signals_t observed) {
    sync_completion_signal(&adapter_sync_);
    // The only signal we expected to receive from the target adapter is |kFinish| after each run.
    return observed == kFinish;
  });
  auto status = target_adapter_->Connect(std::move(eventpair), test_input_.Share());
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
}

bool RunnerImpl::OnSignal() {
  // "Normal" signals are received in response to signals sent to start or finish a run. |RunLoop|
  // keeps track of how many of these signals are sent using |pending_proxy_signals_|.
  auto pending = pending_proxy_signals_.fetch_sub(1);
  FX_DCHECK(pending);
  if (pending == 1) {
    sync_completion_signal(&process_sync_);
  }
  return true;
}

void RunnerImpl::OnError(std::unique_ptr<Error> error) {
  // Only the first proxy to detect an error awakens the |RunLoop|. Subsequent errors are dropped.
  Error* expected = nullptr;
  if (error_.compare_exchange_strong(expected, error.get())) {
    // NOLINTNEXTLINE(bugprone-unused-return-value)
    error.release();  // error_ now owns the struct.
    StopTimer();
    sync_completion_signal(&adapter_sync_);
    sync_completion_signal(&process_sync_);
  }
}

void RunnerImpl::ResetSyncIfNoPendingError(sync_completion_t* sync) {
  // Avoid race by resetting then "unresetting", i.e. signalling, if there's a pending error.
  sync_completion_reset(sync);
  if (error_.load()) {
    sync_completion_signal(sync);
  }
}

bool RunnerImpl::HasError(const Input* last_input) {
  auto* error = error_.load();
  if (!error) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (error->exited) {
    // Almost every error causes the process to exit...
    set_result(error->exited->Join());
  } else {
    /// .. except for timeouts.
    set_result(Result::TIMEOUT);
    constexpr size_t kBufSize = 1ULL << 20;
    auto buf = std::make_unique<char[]>(kBufSize);
    for (auto& proxy : proxies_) {
      auto len = proxy->Dump(buf.get(), kBufSize);
      __sanitizer_log_write(buf.get(), len);
    }
  }
  // If it's an ignored exit(),just remove that one proxy and treat it like a signal.
  if (result() == Result::EXIT && !options_->detect_exits()) {
    proxies_.erase(std::remove_if(proxies_.begin(), proxies_.end(),
                                  [error](const std::unique_ptr<ProcessProxyImpl>& proxy) {
                                    return proxy.get() == error->exited;
                                  }),
                   proxies_.end());
    ClearErrors();
    if (pending_proxy_signals_) {
      OnSignal();
    }
    return false;
  }
  // Otherwise, it's really an error. Remove the target adapter and all proxies.
  coordinator_.Reset();
  proxies_.clear();
  if (last_input) {
    set_result_input(*last_input);
  }
  error_ = nullptr;
  return true;
}

///////////////////////////////////////////////////////////////
// Status-related methods.

Status RunnerImpl::CollectStatusLocked() {
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
  all_stats.reserve(std::min<size_t>(proxies_.size(), MAX_PROCESS_STATS));
  for (auto& proxy : proxies_) {
    if (all_stats.size() == all_stats.capacity()) {
      break;
    }
    ProcessStats stats;
    auto status = proxy->GetStats(&stats);
    if (status == ZX_OK) {
      all_stats.push_back(stats);
    } else {
      FX_LOGS(WARNING) << "Failed to get stats for process: " << zx_status_get_string(status);
    }
  }
  status.set_process_stats(std::move(all_stats));

  return status;
}

}  // namespace fuzzing
