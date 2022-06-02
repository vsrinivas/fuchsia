// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/testing/runner.h"

#include <string.h>

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace fuzzing {

const char* kPattern = "CRASH";

RunnerPtr SimpleFixedRunner::MakePtr(ExecutorPtr executor) {
  return RunnerPtr(new SimpleFixedRunner(std::move(executor)));
}

SimpleFixedRunner::SimpleFixedRunner(ExecutorPtr executor) : Runner(executor), workflow_(this) {
  seed_corpus_.emplace_back();
  live_corpus_.emplace_back();
  start_ = zx::time::infinite();
  pulse_at_ = zx::time::infinite();
}

void SimpleFixedRunner::AddDefaults(Options* options) {
  if (!options->has_runs()) {
    options->set_runs(kDefaultRuns);
  }
  if (!options->has_seed()) {
    options->set_seed(kDefaultSeed);
  }
  if (!options->has_max_input_size()) {
    options->set_max_input_size(kDefaultMaxInputSize);
  }
}

zx_status_t SimpleFixedRunner::AddToCorpus(CorpusType corpus_type, Input input) {
  auto* corpus = corpus_type == CorpusType::SEED ? &seed_corpus_ : &live_corpus_;
  corpus->push_back(std::move(input));
  return ZX_OK;
}

Input SimpleFixedRunner::ReadFromCorpus(CorpusType corpus_type, size_t offset) {
  auto* corpus = corpus_type == CorpusType::SEED ? &seed_corpus_ : &live_corpus_;
  return offset < corpus->size() ? (*corpus)[offset].Duplicate() : Input();
}

zx_status_t SimpleFixedRunner::ParseDictionary(const Input& input) {
  dictionary_ = input.Duplicate();
  return ZX_OK;
}

Input SimpleFixedRunner::GetDictionaryAsInput() const { return dictionary_.Duplicate(); }

ZxPromise<> SimpleFixedRunner::Configure(const OptionsPtr& options) {
  return fpromise::make_promise([this, options]() -> ZxResult<> {
           options_ = options;
           prng_.seed(options_->seed());
           return fpromise::ok();
         })
      .wrap_with(workflow_);
}

ZxPromise<FuzzResult> SimpleFixedRunner::Execute(Input input) {
  return fpromise::make_promise([this, input = std::move(input)]() mutable -> ZxResult<FuzzResult> {
           auto artifact = TestOne(std::move(input));
           return fpromise::ok(artifact.fuzz_result());
         })
      .wrap_with(workflow_);
}

ZxPromise<Input> SimpleFixedRunner::Minimize(Input input) {
  return fpromise::make_promise([this, input = std::move(input)]() mutable -> ZxResult<Input> {
           run_ = 1;
           start_ = zx::clock::get_monotonic();
           pulse_at_ = zx::deadline_after(zx::sec(1));
           UpdateMonitors(UpdateReason::INIT);
           Input minimized = input.Duplicate();
           auto* data = minimized.data();
           auto size = minimized.size();
           auto max_runs = options_->runs();
           // Minimize: just try to remove bytes and see if it still crashes.
           for (; max_runs == 0 || run_ < max_runs; ++run_) {
             bool found = false;
             for (size_t i = 0; i < size; ++i) {
               Input next;
               // Try to shrink the input by one byte.
               next.Reserve(size - 1);
               if (i > 0) {
                 next.Write(data, i);
               }
               if (i < size - 1) {
                 next.Write(&data[i + 1], size - (i + 1));
               }
               auto artifact = TestOne(std::move(next));
               if (artifact.fuzz_result() != FuzzResult::NO_ERRORS) {
                 minimized = artifact.take_input();
                 data = minimized.data();
                 size = minimized.size();
                 found = true;
                 break;
               }
               if (pulse_at_ < zx::clock::get_monotonic()) {
                 pulse_at_ = zx::deadline_after(zx::sec(1));
                 UpdateMonitors(UpdateReason::PULSE);
               }
             }
             if (!found) {
               break;
             }
           }
           UpdateMonitors(UpdateReason::DONE);
           run_ = 0;
           return fpromise::ok(std::move(minimized));
         })
      .wrap_with(workflow_);
}

ZxPromise<Input> SimpleFixedRunner::Cleanse(Input input) {
  return fpromise::make_promise([this, input = std::move(input)]() mutable -> ZxResult<Input> {
           Input cleansed = input.Duplicate();
           auto* data = cleansed.data();
           auto size = cleansed.size();
           // Cleanse: Just try to replace each byte with a space.
           for (size_t i = 0; i < size; ++i) {
             uint8_t original = data[i];
             data[i] = 0x20;
             auto artifact = TestOne(std::move(cleansed));
             cleansed = artifact.take_input();
             if (artifact.fuzz_result() == FuzzResult::NO_ERRORS) {
               data[i] = original;
             }
           }
           return fpromise::ok(std::move(cleansed));
         })
      .wrap_with(workflow_);
}

ZxPromise<Artifact> SimpleFixedRunner::Fuzz() {
  return fpromise::make_promise([this]() -> ZxResult<Artifact> {
           run_ = 1;
           matched_ = 0;
           start_ = zx::clock::get_monotonic();
           pulse_at_ = zx::deadline_after(zx::sec(1));
           UpdateMonitors(UpdateReason::INIT);
           auto max_runs = options_->runs();
           auto max_input_size = options_->max_input_size();
           auto num_seed_inputs = seed_corpus_.size();
           Artifact artifact;
           // Accumulate seed corpus coverage.
           for (size_t offset = 0; offset < num_seed_inputs; ++offset) {
             auto input = ReadFromCorpus(CorpusType::SEED, offset);
             artifact = TestOne(std::move(input));
             if (artifact.fuzz_result() != FuzzResult::NO_ERRORS) {
               // Set |run_| to fall through the subsequent loop.
               run_ = max_runs;
               break;
             }
             input = artifact.take_input();
             Measure(input, /* accumulate */ true);
           }
           // Generate fuzzing inputs and test them.
           for (; max_runs == 0 || run_ < max_runs; ++run_) {
             Input original;
             // Avoid double-counting the empty input that appears in each corpus.
             auto num_inputs = num_seed_inputs + live_corpus_.size() - 1;
             auto offset = prng_() % num_inputs;
             if (offset < num_seed_inputs) {
               original = ReadFromCorpus(CorpusType::SEED, offset);
             } else {
               original = ReadFromCorpus(CorpusType::LIVE, (offset + 1) - num_seed_inputs);
             }
             auto* data = original.data();
             auto size = original.size();
             // Mutate: each time either insert or replace one byte with an uppercase letter.
             bool insert = (size == 0) || (size < max_input_size && (prng_() % 2) == 0);
             Input next;
             next.Reserve(max_input_size);
             size_t i = prng_() % (insert ? size + 1 : size);
             if (i != 0) {
               next.Write(data, i);
             }
             next.Write(static_cast<uint8_t>('A' + (prng_() % 26)));
             if (i < size) {
               if (insert) {
                 next.Write(&data[i], size - i);
               } else {
                 next.Write(&data[i + 1], size - (i + 1));
               }
             }
             artifact = TestOne(std::move(next));
             if (artifact.fuzz_result() != FuzzResult::NO_ERRORS) {
               break;
             }
             next = artifact.take_input();
             if (Measure(next, /* accumulate */ true)) {
               if (auto status = AddToCorpus(CorpusType::LIVE, std::move(next)); status != ZX_OK) {
                 return fpromise::error(status);
               }
               pulse_at_ = zx::deadline_after(zx::sec(1));
               UpdateMonitors(UpdateReason::NEW);
             } else if (pulse_at_ < zx::clock::get_monotonic()) {
               pulse_at_ = zx::deadline_after(zx::sec(1));
               UpdateMonitors(UpdateReason::PULSE);
             }
           }
           UpdateMonitors(UpdateReason::DONE);
           run_ = 0;
           return fpromise::ok(std::move(artifact));
         })
      .wrap_with(workflow_);
}

ZxPromise<> SimpleFixedRunner::Merge() {
  return fpromise::make_promise([this]() -> ZxResult<> {
           matched_ = 0;
           for (const auto& input : seed_corpus_) {
             Measure(input, /* accumulate */ true);
           }
           for (auto& input : live_corpus_) {
             input.set_num_features(Measure(input, /* accumulate */ false));
           }
           std::sort(live_corpus_.begin(), live_corpus_.end());
           std::vector<Input> kept(1);  // Include the empty input.
           for (const auto& input : live_corpus_) {
             if (Measure(input, /* accumulate */ true)) {
               kept.push_back(input.Duplicate());
             }
           }
           live_corpus_ = std::move(kept);
           return fpromise::ok();
         })
      .wrap_with(workflow_);
}

ZxPromise<> SimpleFixedRunner::Stop() { return workflow_.Stop(); }

Status SimpleFixedRunner::CollectStatus() {
  status_.set_running(run_ != 0);
  status_.set_runs(run_);
  if (run_ != 0) {
    auto elapsed = zx::clock::get_monotonic() - start_;
    status_.set_elapsed(elapsed.get());
  }
  size_t total_size = 0;
  size_t max_features = 0;
  for (auto& input : seed_corpus_) {
    total_size += input.size();
    max_features = std::max(max_features, input.num_features());
  }
  for (auto& input : live_corpus_) {
    total_size += input.size();
    max_features = std::max(max_features, input.num_features());
  }
  status_.set_covered_pcs(max_features);
  status_.set_covered_features(max_features);
  // Only count the empty input once.
  status_.set_corpus_num_inputs(seed_corpus_.size() + live_corpus_.size() - 1);
  status_.set_corpus_total_size(total_size);
  return CopyStatus(status_);
}

Artifact SimpleFixedRunner::TestOne(Input input) {
  auto* data = input.data();
  auto size = input.size();
  auto pat_size = strlen(kPattern);
  auto num_candidates = size >= pat_size ? (size - pat_size + 1) : 0;
  for (size_t i = 0; i < num_candidates; ++i) {
    // Crash if the pattern is found in the data.
    if (memcmp(&data[i], kPattern, pat_size) == 0) {
      return Artifact(FuzzResult::CRASH, std::move(input));
    }
  }
  return Artifact(FuzzResult::NO_ERRORS, std::move(input));
}

size_t SimpleFixedRunner::Measure(const Input& input, bool accumulate) {
  const auto* data = reinterpret_cast<const char*>(input.data());
  auto size = input.size();
  size_t matched = 0;
  // Just measure the number of consecutive correct characters.
  for (size_t i = 0; i < size && matched < strlen(kPattern); ++i) {
    if (data[i] == kPattern[matched]) {
      ++matched;
    } else if (matched) {
      break;
    }
  }
  if (matched < matched_) {
    return 0;
  }
  auto new_features = matched - matched_;
  if (accumulate) {
    matched_ = matched;
  }
  return new_features;
}

}  // namespace fuzzing
