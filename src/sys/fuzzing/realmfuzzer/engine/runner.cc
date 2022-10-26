// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/realmfuzzer/engine/runner.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <zircon/sanitizer.h>
#include <zircon/status.h>
#include <zircon/syscalls/exception.h>

#include <deque>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/realmfuzzer/engine/coverage-data.h"
#include "src/sys/fuzzing/realmfuzzer/target/process.h"

namespace fuzzing {

using ::fuchsia::fuzzer::MAX_PROCESS_STATS;

RunnerPtr RealmFuzzerRunner::MakePtr(ExecutorPtr executor) {
  return RunnerPtr(new RealmFuzzerRunner(std::move(executor)));
}

RealmFuzzerRunner::RealmFuzzerRunner(ExecutorPtr executor)
    : Runner(executor),
      generated_receiver_(&generated_sender_),
      leak_receiver_(&leak_sender_),
      adapter_(executor),
      provider_(executor),
      processed_receiver_(&processed_sender_),
      workflow_(this) {
  generated_receiver_.Close();
  processed_receiver_.Close();
  leak_receiver_.Close();

  seed_corpus_ = Corpus::MakePtr();
  live_corpus_ = Corpus::MakePtr();
  pool_ = std::make_shared<ModulePool>();
}

void RealmFuzzerRunner::SetTargetAdapterHandler(TargetAdapterClient::RequestHandler handler) {
  adapter_.set_handler(std::move(handler));
}

zx_status_t RealmFuzzerRunner::BindCoverageDataProvider(zx::channel provider) {
  return provider_.Bind(std::move(provider));
}

ZxPromise<> RealmFuzzerRunner::Configure(const OptionsPtr& options) {
  return fpromise::make_promise([this, options]() -> ZxResult<> {
           options_ = options;
           seed_corpus_->Configure(options_);
           live_corpus_->Configure(options_);
           mutagen_.Configure(options_);
           adapter_.Configure(options_);
           for (auto& [target_id, process_proxy] : process_proxies_) {
             process_proxy->Configure(options_);
           }
           return fpromise::ok();
         })
      .and_then(adapter_.GetParameters().or_else([] {
        FX_LOGS(WARNING) << "Failed to load seed corpora.";
        return fpromise::error(ZX_ERR_CANCELED);
      }))
      .and_then([this](const std::vector<std::string>& parameters) {
        return AsZxResult(seed_corpus_->Load(adapter_.GetSeedCorpusDirectories(parameters)));
      })
      .wrap_with(workflow_);
}

zx_status_t RealmFuzzerRunner::AddToCorpus(CorpusType corpus_type, Input input) {
  switch (corpus_type) {
    case CorpusType::SEED:
      return seed_corpus_->Add(std::move(input));
    case CorpusType::LIVE:
      return live_corpus_->Add(std::move(input));
    default:
      return ZX_ERR_INVALID_ARGS;
  }
}

std::vector<Input> RealmFuzzerRunner::GetCorpus(CorpusType corpus_type) {
  CorpusPtr corpus;
  switch (corpus_type) {
    case CorpusType::SEED:
      corpus = seed_corpus_;
      break;
    case CorpusType::LIVE:
      corpus = live_corpus_;
      break;
    default:
      FX_NOTREACHED();
  }
  // The first input is always the implicit empty input; skip it.
  auto num_inputs = corpus->num_inputs();
  std::vector<Input> inputs(num_inputs - 1);
  for (size_t i = 0; i < inputs.size(); ++i) {
    corpus->At(i + 1, &inputs[i]);
  }
  return inputs;
}

zx_status_t RealmFuzzerRunner::ParseDictionary(const Input& input) {
  Dictionary dict;
  dict.Configure(options_);
  if (!dict.Parse(input)) {
    return ZX_ERR_INVALID_ARGS;
  }
  mutagen_.set_dictionary(std::move(dict));
  return ZX_OK;
}

Input RealmFuzzerRunner::GetDictionaryAsInput() const { return mutagen_.dictionary().AsInput(); }

///////////////////////////////////////////////////////////////
// Asynchronous workflows.

ZxPromise<FuzzResult> RealmFuzzerRunner::Execute(std::vector<Input> inputs) {
  return fpromise::make_promise([this, inputs = std::move(inputs)]() mutable -> ZxResult<> {
           for (auto& input : inputs) {
             if (auto status = generated_sender_.Send(std::move(input)); status != ZX_OK) {
               FX_LOGS(ERROR) << "Input queue closed prematurely: " << zx_status_get_string(status);
               return fpromise::error(status);
             }
           }
           generated_receiver_.Close();
           return fpromise::ok();
         })
      .and_then(TestInputs(kNoPostProcessing))
      .then([](ZxResult<Artifact>& result) -> ZxResult<FuzzResult> {
        if (result.is_ok()) {
          auto artifact = result.take_value();
          return fpromise::ok(artifact.fuzz_result());
        }
        auto status = result.take_error();
        if (status == ZX_ERR_STOP) {
          return fpromise::ok(FuzzResult::NO_ERRORS);
        }
        return fpromise::error(status);
      })
      .wrap_with(workflow_);
}

ZxPromise<Input> RealmFuzzerRunner::Minimize(Input input) {
  auto corpus = live_corpus_;
  auto options = CopyOptions(*options_);
  // Check that the input can be minimized, and that minimizationis bounded.
  return TestOneAsync(std::move(input), kNoPostProcessing)
      .or_else([](const zx_status_t& status) {
        if (status == ZX_ERR_STOP) {
          FX_LOGS(WARNING) << "Test input did not trigger an error.";
          return fpromise::error(ZX_ERR_INVALID_ARGS);
        }
        return fpromise::error(status);
      })
      .and_then([this](Artifact& artifact) -> ZxResult<Artifact> {
        if (!options_->has_runs() && !options_->has_max_total_time()) {
          FX_LOGS(INFO)
              << "'max_total_time' and 'runs' are both not set. Defaulting to 10 minutes.";
          options_->set_max_total_time(zx::min(10).get());
        }
        return fpromise::ok(std::move(artifact));
      })
      .and_then([this, fuzz_result = FuzzResult::NO_ERRORS, input = Input(),
                 minimize = ZxFuture<Artifact>()](Context& context,
                                                  Artifact& original) mutable -> ZxResult<Input> {
        if (fuzz_result == FuzzResult::NO_ERRORS) {
          // First pass.
          std::tie(fuzz_result, input) = original.take_tuple();
        }
        while (true) {
          if (!minimize) {
            // Ratchet down the input one byte.
            if (input.size() < 2) {
              FX_LOGS(INFO) << "Input is " << input.size()
                            << " byte(s); will not minimize further.";
              return fpromise::ok(std::move(input));
            }
            auto next_input = input.Duplicate();
            next_input.Truncate(input.size() - 1);
            options_->set_max_input_size(next_input.size());
            // Start each fuzzing pass using the seed corpus and the minimized input.
            live_corpus_ = Corpus::MakePtr();
            live_corpus_->Configure(options_);
            auto status = live_corpus_->Add(std::move(next_input));
            if (status != ZX_OK) {
              FX_LOGS(ERROR) << "Failed to reset corpus: " << zx_status_get_string(status);
              return fpromise::error(status);
            }
            // Imitate libFuzzer and count from 0 so long as errors are found.
            Reset();
            run_ = 0;
            pool_->Clear();
            minimize = FuzzInputs();
          }
          if (!minimize(context)) {
            return fpromise::pending();
          }
          if (minimize.is_error()) {
            return fpromise::error(minimize.error());
          }
          auto artifact = minimize.take_value();
          if (artifact.fuzz_result() == FuzzResult::NO_ERRORS) {
            FX_LOGS(INFO) << "Did not reduce error input beyond " << input.size()
                          << " bytes; exiting.";
            return fpromise::ok(std::move(input));
          }
          // TODO(fxbug.dev/85424): This needs a more rigorous way of deduplicating crashes.
          if (artifact.fuzz_result() != fuzz_result) {
            FX_LOGS(WARNING) << "Different error detected; will not minimize further.";
            return fpromise::ok(std::move(input));
          }
          input = artifact.take_input();
        }
      })
      .then([this, corpus, options = std::move(options)](ZxResult<Input>& result) mutable {
        pool_->Clear();
        live_corpus_ = corpus;
        *options_ = std::move(options);
        return std::move(result);
      })
      .wrap_with(workflow_);
}

ZxPromise<Input> RealmFuzzerRunner::Cleanse(Input input) {
  // The general approach of this loop is to take tested inputs and their fuzzing results and return
  // them to |GenerateCleanInputs| as |Artifacts|.
  AsyncSender<Artifact> sender;
  auto receiver = AsyncReceiver<Artifact>::MakePtr(&sender);
  return fpromise::make_promise([this, generate = Future<>(), sender = std::move(sender),
                                 receiver = std::move(receiver), test_inputs = ZxFuture<Artifact>(),
                                 receive = Future<Input>(),
                                 result = Artifact(FuzzResult::NO_ERRORS, std::move(input)),
                                 artifacts = std::array<Artifact, 2>(),
                                 num_artifacts = 0U](Context& context) mutable -> ZxResult<Input> {
           while (true) {
             if (!generate) {
               // To set up initial conditions, simulate having just completed an "extra" attempt.
               constexpr size_t kMaxCleanseAttempts = 5;
               auto attempts_left = kMaxCleanseAttempts + 1;

               // Prepare the pipeline with some artifacts that make the attempt succeed and won't
               // be reverted. This only fails if the `sender` is closed, in which case the promise
               // below returns an error.
               for (size_t i = 0; i < 2; ++i) {
                 if (auto status =
                         sender.Send(Artifact(FuzzResult::CRASH, result.input().Duplicate()));
                     status != ZX_OK) {
                   FX_LOGS(ERROR) << "Failed to prepare fuzzing input pipeline: "
                                  << zx_status_get_string(status);
                 }
               }
               generate = GenerateCleanInputs(std::move(receiver), attempts_left);
             }
             if (!test_inputs) {
               test_inputs = TestInputs(kNoPostProcessing);
             }
             if (!receive) {
               receive = processed_receiver_.Receive();
             }
             if (generate(context) && generate.is_error()) {
               // |GenerateCleanInputs| only returns an error if its queues close unexpectedly.
               return fpromise::error(ZX_ERR_BAD_STATE);
             }
             if (test_inputs(context)) {
               if (test_inputs.is_error()) {
                 auto status = test_inputs.error();
                 if (status != ZX_ERR_STOP) {
                   return fpromise::error(status);
                 }
                 return fpromise::ok(result.take_input());
               }
               // Cleansed input triggered an error. Use it as the basis for further attempts.
               result = test_inputs.take_value();
               artifacts[0] = result.Duplicate();
               artifacts[1] = result.Duplicate();
               receive = nullptr;
               Reset();
               num_artifacts = 2;
             } else if (receive(context)) {
               if (receive.is_error()) {
                 FX_LOGS(ERROR) << "Output queue closed unexpectedly.";
                 return fpromise::error(ZX_ERR_BAD_STATE);
               }
               // Cleansed input didn't trigger an error. Save it for recycling.
               artifacts[num_artifacts++] = Artifact(FuzzResult::NO_ERRORS, receive.take_value());
             } else {
               // Still testing an input.
               return fpromise::pending();
             }
             if (num_artifacts < artifacts.size()) {
               continue;
             }
             // Recycle inputs in pairs, one for each "clean" byte.
             if (sender.Send(std::move(artifacts[0])) != ZX_OK ||
                 sender.Send(std::move(artifacts[1])) != ZX_OK) {
               // No more inputs are needed; all done.
               return fpromise::ok(result.take_input());
             }
             num_artifacts = 0;
           }
         })
      .wrap_with(workflow_);
}

ZxPromise<Artifact> RealmFuzzerRunner::Fuzz() {
  return FuzzInputs(/* backlog= */ options_->mutation_depth()).wrap_with(workflow_);
}

ZxPromise<> RealmFuzzerRunner::Merge() {
  // First, accumulate the coverage from testing all the elements of the seed corpus.
  auto collect_errors = std::make_shared<std::vector<Input>>();
  return TestOneAsync(Input(), kAccumulateCoverage)
      .or_else([this](const zx_status_t& status) {
        return CheckPrevious(status).and_then(TestCorpusAsync(seed_corpus_, kAccumulateCoverage));
      })
      .and_then([](const Artifact& artifact) -> ZxResult<Artifact> {
        FX_LOGS(WARNING) << "Seed corpus contains an input that triggers an error: '"
                         << artifact.input().ToHex() << "'";
        return fpromise::error(ZX_ERR_INVALID_ARGS);
      })
      .or_else([this, collect_errors](const zx_status_t& status) {
        return CheckPrevious(status).and_then([this, collect_errors] {
          // Next, measure what coverage each element of the live corpus provides beyond that
          // accumulated by the seed corpus. After this step the live corpus contains only valid,
          // measured inputs.
          auto unmeasured = live_corpus_;
          live_corpus_ = Corpus::MakePtr();
          live_corpus_->Configure(options_);
          return TestCorpusAsync(unmeasured, kMeasureCoverageAndKeepInputs, collect_errors);
        });
      })
      .or_else([this, collect_errors](const zx_status_t& status) {
        return CheckPrevious(status).and_then([this, collect_errors] {
          if (!collect_errors->empty()) {
            FX_LOGS(WARNING) << "Corpus contains input(s) that trigger error(s):";
            for (auto& input : *collect_errors) {
              FX_LOGS(WARNING) << "  '" << input.ToHex() << "'";
            }
          }
          // Finally, accumulate the coverage from each element of the live corpus. The live corpus
          // will be stably sorted by size, number of features measured above, and lexicographical
          // order. Only elements that add coverage not accumulated by previous elements will be
          // kept.
          auto measured = live_corpus_;
          live_corpus_ = Corpus::MakePtr();
          live_corpus_->Configure(options_);
          return TestCorpusAsync(measured, kAccumulateCoverageAndKeepInputs);
        });
      })
      .and_then([](const Artifact& artifact) -> ZxResult<> {
        FX_LOGS(ERROR) << "Previously successful input triggered an error: '"
                       << artifact.input().ToHex() << "'";
        return fpromise::error(ZX_ERR_BAD_STATE);
      })
      .or_else([this, collect_errors](const zx_status_t& status) {
        return CheckPrevious(status).and_then([this, collect_errors]() -> ZxResult<> {
          // As a final step, keep any inputs that triggered errors.
          for (auto& input : *collect_errors) {
            if (auto status = live_corpus_->Add(std::move(input)); status != ZX_OK) {
              return fpromise::error(status);
            }
          }
          return fpromise::ok();
        });
      })
      .wrap_with(workflow_);
}

ZxPromise<> RealmFuzzerRunner::Stop() {
  stopped_ = true;
  return workflow_.Stop();
}

Status RealmFuzzerRunner::CollectStatus() {
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
  status.set_process_stats(std::move(all_stats));

  return status;
}

///////////////////////////////////////////////////////////////
// Workflow-related methods.

void RealmFuzzerRunner::StartWorkflow(Scope& scope) {
  Reset();
  run_ = 0;
  pool_->Clear();
  start_ = zx::clock::get_monotonic();
  pulse_start_ = start_ + zx::sec(2);
  stopped_ = false;
  // Handle coverage data produced during the workflow.
  auto task = fpromise::make_promise([this, get_coverage_data = Future<CoverageData>()](
                                         Context& context) mutable -> Result<> {
                while (true) {
                  if (!get_coverage_data) {
                    get_coverage_data = provider_.GetCoverageData();
                  }
                  if (!get_coverage_data(context)) {
                    return fpromise::pending();
                  }
                  if (get_coverage_data.is_error()) {
                    return fpromise::error();
                  }
                  auto coverage_data = get_coverage_data.take_value();
                  switch (coverage_data.Which()) {
                    case CoverageData::Tag::kInstrumented:
                      ConnectProcess(coverage_data.instrumented());
                      break;
                    case CoverageData::Tag::kInline8bitCounters:
                      AddLlvmModule(coverage_data.inline_8bit_counters());
                      break;
                    default:
                      FX_LOGS(WARNING)
                          << "Unrecgonized coverage data type: " << coverage_data.Which();
                      return fpromise::error();
                  }
                }
              }).wrap_with(scope);
  executor()->schedule_task(std::move(task));
  UpdateMonitors(UpdateReason::INIT);
}

void RealmFuzzerRunner::FinishWorkflow() {
  generated_receiver_.Clear();
  processed_receiver_.Clear();
  leak_receiver_.Clear();
  stopped_ = true;
  UpdateMonitors(UpdateReason::DONE);
}

///////////////////////////////////////////////////////////////
// Methods to generate fuzzing inputs.

ZxPromise<> RealmFuzzerRunner::GenerateInputs(size_t num_inputs, size_t backlog) {
  // Set up parameters for determining what inputs to generate and for how long.
  auto max_size = options_->max_input_size();
  auto max_time = zx::duration(options_->max_total_time());
  auto deadline = max_time.get() ? zx::deadline_after(max_time) : zx::time::infinite();
  auto mutation_depth = options_->mutation_depth();
  return fpromise::make_promise([this, backlog, max_size]() -> ZxResult<> {
           // "Precycle" some inputs by making it look like they are ready for reuse.
           for (size_t i = 0; i <= backlog; i++) {
             auto status = processed_sender_.Send(Input(max_size));
             if (status != ZX_OK) {
               FX_LOGS(ERROR) << "Input queue closed prematurely while preparing to fuzz: "
                              << zx_status_get_string(status);
               return fpromise::error(status);
             }
           }
           return fpromise::ok();
         })
      .and_then([this, num_sent = 0U, num_inputs, deadline, num_mutations = mutation_depth,
                 mutation_depth,
                 recycle = Future<Input>()](Context& context) mutable -> ZxResult<> {
        while (true) {
          if (num_inputs != 0 && num_sent >= num_inputs) {
            // Run limit will be reached by inputs already queued; all done.
            return fpromise::ok();
          }
          if (zx::clock::get_monotonic() >= deadline) {
            // Time limit reached; all done.
            return fpromise::ok();
          }
          if (stopped_) {
            // Interrupted; all done.
            return fpromise::ok();
          }
          if (!recycle) {
            // Use inputs recycled from earlier runs to reduce heap allocations.
            recycle = processed_receiver_.Receive();
          }
          if (!recycle(context)) {
            return fpromise::pending();
          }
          if (recycle.is_error()) {
            // Queue was closed; all done.
            return fpromise::ok();
          }
          auto input = recycle.take_value();
          if (num_mutations >= mutation_depth) {
            // Pick an input and mutate it |mutation_depth| times in a row.
            mutagen_.reset_mutations();
            live_corpus_->Pick(mutagen_.base_input());
            live_corpus_->Pick(mutagen_.crossover());
            num_mutations = 0;
          }
          mutagen_.Mutate(&input);
          ++num_mutations;
          auto status = generated_sender_.Send(std::move(input));
          num_sent++;
          if (status != ZX_OK) {
            // Queue was closed; all done.
            return fpromise::ok();
          }
        }
      })
      .and_then([this] {
        generated_receiver_.Close();
        return fpromise::ok();
      });
}

Promise<> RealmFuzzerRunner::GenerateCleanInputs(AsyncReceiverPtr<Artifact> receiver,
                                                 size_t attempts_left) {
  // Ensure that a new attempt will be started.
  auto offset = std::numeric_limits<size_t>::max() - 1;

  // The general approach is to produce two inputs at a time, each with one byte replaced by a
  // space or 0xff. Bytes that are already a space or 0xff are skipped. Each iteration over all
  // input bytes is an attempt, and inputs are produced until an attempt doesn't produce any errors
  // or five attempts have been performed.
  return fpromise::make_promise([this, receiver = std::move(receiver), receive = Future<Artifact>(),
                                 artifacts = std::array<Artifact, 2>(), num_artifacts = 0U,
                                 attempts_left, offset, found_error = false,
                                 original = uint8_t(0)](Context& context) mutable -> Result<> {
    while (true) {
      // Recycle two artifacts.
      if (!receive) {
        receive = receiver->Receive();
      }
      if (!receive(context)) {
        return fpromise::pending();
      }
      if (receive.is_error()) {
        FX_LOGS(ERROR) << "Recycled input queue closed unexpectedly.";
        return fpromise::error();
      }
      artifacts[num_artifacts++] = receive.take_value();
      if (num_artifacts < artifacts.size()) {
        continue;
      }
      auto fuzz_result = artifacts[0].fuzz_result();
      auto input0 = artifacts[0].take_input();
      auto input1 = artifacts[1].take_input();
      auto* data0 = input0.data();
      auto* data1 = input1.data();
      if (fuzz_result == FuzzResult::NO_ERRORS) {
        // Last inputs didn't trigger any errors; restore the modified byte.
        data0[offset] = original;
        data1[offset] = original;
      } else {
        found_error = true;
      }
      // Find a "cleanable" byte, i.e. one that isn't already 0x20 or 0xff.
      do {
        if (++offset < input0.size()) {
          // Continue the current attempt.
          continue;
        }
        // Reached the end of the input. Start a new attempt.
        offset = 0;
        if (--attempts_left == 0 || !found_error) {
          // Out of attempts, or last attempt didn't trigger any error. All done.
          receiver->Close();
          generated_receiver_.Close();
          return fpromise::ok();
        }
        found_error = false;
      } while (data0[offset] == 0x20 || data0[offset] == 0xff);
      // Now actually clean the byte and send them to be tested.
      original = data0[offset];
      data0[offset] = 0x20;
      data1[offset] = 0xff;
      if (generated_sender_.Send(std::move(input0)) != ZX_OK ||
          generated_sender_.Send(std::move(input1)) != ZX_OK) {
        FX_LOGS(ERROR) << "Input queue unexpectedly closed.";
        return fpromise::error();
      }
      num_artifacts = 0;
    }
  });
}

///////////////////////////////////////////////////////////////
// Methods to perform a sequence of fuzzing runs.

ZxPromise<Artifact> RealmFuzzerRunner::FuzzInputs(size_t backlog) {
  auto num_inputs = options_->runs();
  if (num_inputs != 0) {
    // Adjust for fixed inputs tested first. Be careful not to double count the empty input.
    num_inputs -= (seed_corpus_->num_inputs() + live_corpus_->num_inputs() - 1);
  }
  return TestOneAsync(Input(), kAccumulateCoverage)
      .or_else([this](const zx_status_t& status) {
        return CheckPrevious(status).and_then(
            [this] { return TestCorpusAsync(seed_corpus_, kAccumulateCoverage); });
      })
      .or_else([this](const zx_status_t& status) {
        return CheckPrevious(status).and_then(
            [this] { return TestCorpusAsync(live_corpus_, kAccumulateCoverage); });
      })
      .or_else([this, backlog, num_inputs](const zx_status_t& status) {
        live_corpus_->Add(seed_corpus_);
        return CheckPrevious(status).and_then(
            [generate = ZxFuture<>(GenerateInputs(num_inputs, backlog)),
             test = ZxFuture<Artifact>(TestInputs(kAccumulateCoverageAndKeepInputs))](
                Context& context) mutable -> ZxResult<Artifact> {
              if (generate(context) && generate.is_error()) {
                return fpromise::error(generate.error());
              }
              if (!test(context)) {
                return fpromise::pending();
              }
              return test.take_result();
            });
      })
      .or_else([this](const zx_status_t& status) {
        return CheckPrevious(status).and_then([]() -> ZxResult<Artifact> {
          // Finished without finding an input that causes an error; return an empty artifact.
          return fpromise::ok(Artifact());
        });
      });
}

ZxPromise<Artifact> RealmFuzzerRunner::TestOneAsync(Input input, PostProcessing mode) {
  return fpromise::make_promise([this, input = std::move(input)]() mutable -> ZxResult<> {
           return AsZxResult(generated_sender_.Send(std::move(input)));
         })
      .and_then([this]() {
        generated_receiver_.Close();
        return fpromise::ok();
      })
      .and_then([this, mode] { return TestInputs(mode); });
}

ZxPromise<Artifact> RealmFuzzerRunner::TestCorpusAsync(CorpusPtr corpus, PostProcessing mode,
                                                       InputsPtr collect_errors) {
  return fpromise::make_promise([this]() -> ZxResult<> {
           // Prime the output queue.
           Reset();
           return AsZxResult(processed_sender_.Send(Input()));
         })
      .and_then([this, corpus, mode, collect_errors, test_inputs = ZxFuture<Artifact>(),
                 receive = Future<Input>(),
                 offset = 1U](Context& context) mutable -> ZxResult<Artifact> {
        while (true) {
          if (!test_inputs) {
            test_inputs = TestInputs(mode, collect_errors);
          }
          if (!receive) {
            receive = processed_receiver_.Receive();
          }
          if (test_inputs(context)) {
            // Done testing inputs.
            Reset();
            return test_inputs.take_result();
          }
          if (!receive(context)) {
            // Still testing.
            return fpromise::pending();
          }
          // Ready for the next input from the corpus.
          if (receive.is_error()) {
            FX_LOGS(ERROR) << "Output queue closed prematurely.";
            return fpromise::error(ZX_ERR_BAD_STATE);
          }
          auto input = receive.take_value();
          if (!corpus->At(offset++, &input)) {
            generated_receiver_.Close();
            continue;
          }
          if (auto status = generated_sender_.Send(std::move(input)); status != ZX_OK) {
            FX_LOGS(ERROR) << "Input queue closed prematurely: " << zx_status_get_string(status);
            return fpromise::error(status);
          }
        }
      });
}

ZxPromise<Artifact> RealmFuzzerRunner::TestInputs(PostProcessing mode, InputsPtr collect_errors) {
  constexpr size_t kMaxLeakDetectionAttempts = 1000;
  auto leak_detections = options_->detect_leaks() ? kMaxLeakDetectionAttempts : 0;
  return fpromise::make_promise(
      [this, mode, collect_errors, input = Input(), leak_detections, detect_leaks = false,
       prepare = ZxFuture<Input>(),
       run = Future<bool, FuzzResult>()](Context& context) mutable -> ZxResult<Artifact> {
        while (true) {
          // Reset process coverage and get a new input.
          if (!prepare) {
            prepare = Prepare(detect_leaks);
          }
          if (!prepare(context)) {
            return fpromise::pending();
          }
          // Make sure no errors have been received before testing an input.
          if (prepare.is_error()) {
            return fpromise::error(prepare.error());
          }
          if (!run) {
            input = std::move(prepare.value());
            run = RunOne(input);
          }
          // Now check if the run has finished and if any process reported an error.
          if (!run(context)) {
            return fpromise::pending();
          }
          auto leak_suspected = false;
          if (run.is_ok()) {
            leak_suspected = run.take_value();
          } else if (collect_errors) {
            // If collecting errors, clear errors and continue. Simulate already having attempted to
            // |detect_leaks| to true to skip analysis and leak detection.
            collect_errors->emplace_back(std::move(input));
            detect_leaks = true;
            run = nullptr;
          } else {
            return fpromise::ok(Artifact(run.error(), std::move(input)));
          }
          // Skip post-processing when repeating inputs for leak detection.
          if (!detect_leaks) {
            Analyze(input, mode);
          }
          // Iteration complete! Clear the futures so that the loop starts from the top again.
          detect_leaks = Recycle(std::move(input), leak_detections, leak_suspected, detect_leaks);
          prepare = nullptr;
        }
      });
}

///////////////////////////////////////////////////////////////
// Methods to perform individual steps of a single fuzzing run.

ZxPromise<> RealmFuzzerRunner::CheckPrevious(zx_status_t status) {
  return fpromise::make_promise([status]() -> ZxResult<> {
    if (status != ZX_ERR_STOP) {
      return fpromise::error(status);
    }
    return fpromise::ok();
  });
}

ZxPromise<Input> RealmFuzzerRunner::Prepare(bool detect_leaks) {
  return fpromise::make_promise([this, detect_leaks]() {
           // Send start signals.
           std::vector<ZxPromise<>> starts;
           for (auto& [target_id, process_proxy] : process_proxies_) {
             starts.emplace_back(process_proxy->Start(detect_leaks));
           };
           // Wait for processes to acknowledge.
           return fpromise::join_promise_vector(std::move(starts));
         })
      .then([](Result<std::vector<ZxResult<>>>& results) -> ZxResult<> {
        for (auto& result : results.value()) {
          if (result.is_error()) {
            // Ideally, processes should only return errors as a result of testing inputs.
            FX_LOGS(WARNING)
                << "Detected error between fuzzing runs. This error cannot be associated "
                   "with a specific input. The fuzzer may be non-deterministic and/or "
                   "non-hermetic, and may need to be modified to make results more "
                   "reproducible.";
            return fpromise::error(ZX_ERR_BAD_STATE);
          }
        }
        return fpromise::ok();
      })
      .and_then([this, leak = Future<Input>(),
                 generate = Future<Input>()](Context& context) mutable -> ZxResult<Input> {
        if (!leak) {
          leak = leak_receiver_.Receive();
        }
        if (!generate) {
          generate = generated_receiver_.Receive();
        }
        if (leak(context)) {
          if (leak.is_error()) {
            return fpromise::error(ZX_ERR_STOP);
          }
          return fpromise::ok(leak.take_value());
        }
        if (generate(context)) {
          if (generate.is_error()) {
            // No more inputs means the workflow is done.
            return fpromise::error(ZX_ERR_STOP);
          }
          return fpromise::ok(generate.take_value());
        }
        return fpromise::pending();
      });
}

Promise<bool, FuzzResult> RealmFuzzerRunner::RunOne(const Input& input) {
  return fpromise::make_promise([this, &input, run_limit = options_->run_limit(),
                                 timeout = Future<>(),
                                 first = true](Context& context) mutable -> Result<bool, uint64_t> {
           // Create a future for the per-run timeout. If this completes, it's an error.
           if (run_limit && !timeout) {
             timeout = executor()->MakeDelayedPromise(zx::duration(run_limit));
           }
           if (run_limit && timeout(context)) {
             return fpromise::error(kTimeoutTargetId);
           }
           if (first) {
             ++run_;
             for (auto& [target_id, process_proxy] : process_proxies_) {
               futures_.emplace_back(process_proxy->AwaitFinish());
             }
             futures_.emplace_back(adapter_.TestOneInput(input)
                                       .or_else([] { return fpromise::error(kInvalidTargetId); })
                                       .and_then([this]() -> Result<bool, uint64_t> {
                                         for (auto& [target_id, process_proxy] : process_proxies_) {
                                           if (auto status = process_proxy->Finish();
                                               status != ZX_OK) {
                                             FX_LOGS(WARNING) << "Failed to signal process: "
                                                              << zx_status_get_string(status);
                                           }
                                         }
                                         return fpromise::ok(false);
                                       }));
             first = false;
           }
           auto all_done = true;
           auto leak_suspected = false;
           for (auto& future : futures_) {
             if (!future(context)) {
               all_done = false;
               continue;
             }
             if (future.is_error()) {
               return fpromise::error(future.error());
             }
             leak_suspected |= future.value();
           }
           if (!all_done) {
             suspended_ = context.suspend_task();
             return fpromise::pending();
           }
           return fpromise::ok(leak_suspected);
         })
      .inspect([this](const Result<bool, uint64_t>& ignored) { futures_.clear(); })
      .or_else([this](const uint64_t& target_id) { return GetFuzzResult(target_id); });
}

void RealmFuzzerRunner::ConnectProcess(InstrumentedProcess& instrumented) {
  auto process_proxy = std::make_unique<ProcessProxy>(executor(), pool_);
  process_proxy->Configure(options_);
  if (auto status = process_proxy->Connect(instrumented); status != ZX_OK) {
    FX_LOGS(WARNING) << "Failed to add process: " << zx_status_get_string(status);
    return;
  }
  auto target_id = process_proxy->target_id();
  futures_.emplace_back(process_proxy->AwaitFinish());
  process_proxies_[target_id] = std::move(process_proxy);
  // Kick |RunOne| to check the |AwaitFinish| future.
  suspended_.resume_task();
}

void RealmFuzzerRunner::AddLlvmModule(zx::vmo& inline_8bit_counters) {
  auto target_id = GetTargetId(inline_8bit_counters);
  auto iter = process_proxies_.find(target_id);
  if (iter == process_proxies_.end()) {
    FX_LOGS(WARNING) << "Failed to add module: no such target_id: " << target_id;
    return;
  }
  auto& process_proxy = iter->second;
  if (auto status = process_proxy->AddModule(inline_8bit_counters); status != ZX_OK) {
    FX_LOGS(WARNING) << "Failed to add module: " << zx_status_get_string(status);
    return;
  }
}

Promise<bool, FuzzResult> RealmFuzzerRunner::GetFuzzResult(uint64_t target_id) {
  return fpromise::make_promise(
             [this, target_id, process_proxy = std::unique_ptr<ProcessProxy>(),
              result = ZxFuture<FuzzResult>()](Context& context) mutable -> ZxResult<FuzzResult> {
               if (target_id == kTimeoutTargetId) {
                 // For timeouts, dump all threads to the sanitizer log.
                 constexpr size_t kBufSize = 1ULL << 20;
                 auto buf = std::make_unique<char[]>(kBufSize);
                 for (auto& [target_id, proxy] : process_proxies_) {
                   auto len = proxy->Dump(buf.get(), kBufSize);
                   __sanitizer_log_write(buf.get(), len);
                 }
                 return fpromise::ok(FuzzResult::TIMEOUT);
               }
               if (!result) {
                 // For all other errors, wait on the result from the process exitcode.
                 auto iter = process_proxies_.find(target_id);
                 if (iter == process_proxies_.end()) {
                   FX_LOGS(ERROR) << "Cannot get error from unknown target_id: 0x" << std::hex
                                  << target_id;
                   return fpromise::error(ZX_ERR_NOT_FOUND);
                 }
                 process_proxy = std::move(iter->second);
                 process_proxies_.erase(iter);
                 result = process_proxy->GetResult();
               }
               if (!result(context)) {
                 return fpromise::pending();
               }
               return result.take_result();
             })
      .or_else([](const zx_status_t& status) -> Result<FuzzResult, FuzzResult> {
        FX_LOGS(WARNING) << "Failed to get result: " << zx_status_get_string(status);
        FX_LOGS(WARNING) << "Defaulting to error type of 'crash'.";
        return fpromise::ok(FuzzResult::CRASH);
      })
      .and_then([this](const FuzzResult& fuzz_result) -> Result<bool, FuzzResult> {
        if (fuzz_result == FuzzResult::NO_ERRORS) {
          return fpromise::ok(false);
        }
        // If it's an ignored exit(),just remove that one process_proxy and treat it like a success.
        if (fuzz_result == FuzzResult::EXIT && !options_->detect_exits()) {
          return fpromise::ok(false);
        }
        // Otherwise, it's really an error. Remove the target adapter and all proxies.
        adapter_.Disconnect();
        process_proxies_.clear();
        return fpromise::error(fuzz_result);
      });
}

void RealmFuzzerRunner::Analyze(Input& input, PostProcessing mode) {
  bool updated = false;
  switch (mode) {
    case kNoPostProcessing: {
      break;
    }
    case kAccumulateCoverage: {
      pool_->Accumulate();
      break;
    }
    case kMeasureCoverageAndKeepInputs: {
      auto num_features = pool_->Measure();
      if (num_features) {
        input.set_num_features(num_features);
        if (auto status = live_corpus_->Add(std::move(input)); status != ZX_OK) {
          FX_LOGS(WARNING) << "Failed to save input: " << zx_status_get_string(status);
        }
      }
      break;
    }
    case kAccumulateCoverageAndKeepInputs: {
      if (pool_->Accumulate()) {
        if (auto status = live_corpus_->Add(std::move(input)); status != ZX_OK) {
          FX_LOGS(WARNING) << "Failed to save input: " << zx_status_get_string(status);
        }
        UpdateMonitors(UpdateReason::NEW);
        updated = true;
      }
      break;
    }
  }
  // After a few seconds, reassure the user that the fuzzer is running by reporting each run that
  // equals a power of 2, unless it was already reported above.
  if (!updated && (run_ & (run_ - 1)) == 0 && zx::clock::get_monotonic() > pulse_start_) {
    UpdateMonitors(UpdateReason::PULSE);
  }
}

bool RealmFuzzerRunner::Recycle(Input&& input, size_t& attempts_left, bool suspected,
                                bool detecting) {
  // Determine if leak detection is needed and thereby where to send the input. Leak detection is
  // expensive, so the strategy is as follows:
  // 1. Try inputs once without leak detection.
  // 2. If leak detection is requested, check if leaks are suspected (unbalanced malloc/frees).
  // 3. If a leak if suspected, do the normal feedback analysis and then try the input again, this
  //    time with leak detection. Skip the feedback analysis on the second try.
  // 4. Keep track of how many suspected leaks don't result in an error. After
  //    |kMaxLeakDetections|, disable further leak detection.
  if (attempts_left) {
    if (detecting) {
      // Already tried detecting a leak. Decrement the number of attempts.
      --attempts_left;
      if (attempts_left == 0) {
        FX_LOGS(INFO) << "Disabling leak detection: No memory leaks have been found in any inputs "
                      << "suspected of leaking. Memory may be accumulating in some global state "
                      << "without leaking. End-of-process leak checks will still be performed.";
      }
    } else if (suspected) {
      // Leak detection is still possible, and the last run exhibited a suspected leak. Push the
      // input to the front of the queue to retry with leak detection.

      if (auto status = leak_sender_.Send(std::move(input)); status != ZX_OK) {
        FX_LOGS(WARNING) << "Failed to resend input: " << zx_status_get_string(status);
      } else {
        return true;
      }
    }
  }
  // Send input to be recycled.
  if (auto status = processed_sender_.Send(std::move(input)); status != ZX_OK) {
    FX_LOGS(WARNING) << "Failed to recycle input: " << zx_status_get_string(status);
  }
  return false;
}

///////////////////////////////////////////////////////////////
// Clean-up methods.

void RealmFuzzerRunner::Disconnect() {
  adapter_.Disconnect();
  process_proxies_.clear();
}

void RealmFuzzerRunner::Reset() {
  generated_receiver_.Reset();
  processed_receiver_.Reset();
  leak_receiver_.Reset();
}

}  // namespace fuzzing
