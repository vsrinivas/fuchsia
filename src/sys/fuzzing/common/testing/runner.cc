// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/testing/runner.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <iostream>

#include "src/sys/fuzzing/common/status.h"

namespace fuzzing {
namespace {

const char* kCrash = "CRASH";
const size_t kCrashLen = 5;

std::string as_string(const Input& input) {
  return input.size() != 0 ? std::string(reinterpret_cast<const char*>(input.data()), input.size())
                           : std::string();
}

size_t get_prefix_len(const std::string& input) {
  auto max = std::min(input.size(), kCrashLen);
  for (size_t i = 0; i < max; ++i) {
    if (input[i] != kCrash[i]) {
      return i;
    }
  }
  return max;
}

}  // namespace

RunnerPtr FakeRunner::MakePtr(ExecutorPtr executor) {
  return RunnerPtr(new FakeRunner(std::move(executor)));
}

FakeRunner::FakeRunner(ExecutorPtr executor) : Runner(executor), workflow_(this) {
  seed_corpus_.emplace_back(Input());
  live_corpus_.emplace_back(Input());
}

zx_status_t FakeRunner::AddToCorpus(CorpusType corpus_type, Input input) {
  auto* corpus = corpus_type == CorpusType::SEED ? &seed_corpus_ : &live_corpus_;
  corpus->emplace_back(std::move(input));
  return ZX_OK;
}

Input FakeRunner::ReadFromCorpus(CorpusType corpus_type, size_t offset) {
  auto* corpus = corpus_type == CorpusType::SEED ? &seed_corpus_ : &live_corpus_;
  return offset < corpus->size() ? (*corpus)[offset].Duplicate() : Input();
}

zx_status_t FakeRunner::ParseDictionary(const Input& input) {
  if (input == FakeRunner::invalid_dictionary()) {
    return ZX_ERR_INVALID_ARGS;
  }
  dictionary_ = input.Duplicate();
  return ZX_OK;
}

Input FakeRunner::GetDictionaryAsInput() const { return dictionary_.Duplicate(); }

ZxPromise<> FakeRunner::Configure(const OptionsPtr& options) {
  return fpromise::make_promise([this, options]() -> ZxResult<> {
           options_ = options;
           return fpromise::ok();
         })
      .wrap_with(workflow_);
}

ZxPromise<FuzzResult> FakeRunner::Execute(std::vector<Input> inputs) {
  return Run()
      .and_then([inputs = std::move(inputs)](const Artifact& artifact) {
        if (artifact.fuzz_result() != FuzzResult::NO_ERRORS) {
          return fpromise::ok(artifact.fuzz_result());
        }
        // If no result was set up, crash if the input contains |kCrash|.
        for (auto& input : inputs) {
          auto pos = as_string(input).find(kCrash);
          if (pos != std::string::npos) {
            return fpromise::ok(FuzzResult::CRASH);
          }
        }
        return fpromise::ok(FuzzResult::NO_ERRORS);
      })
      .wrap_with(workflow_);
}

ZxPromise<Input> FakeRunner::Minimize(Input input) {
  return Run()
      .and_then([input = std::move(input)](Artifact& artifact) {
        if (artifact.input().size() != 0) {
          return fpromise::ok(artifact.take_input());
        }
        // If no result was set up, remove all bytes except |kCrash|.
        auto pos = as_string(input).find(kCrash);
        return fpromise::ok(pos != std::string::npos ? Input(kCrash) : Input());
      })
      .wrap_with(workflow_);
}

ZxPromise<Input> FakeRunner::Cleanse(Input input) {
  return Run()
      .and_then([input = std::move(input)](Artifact& artifact) {
        if (artifact.input().size() != 0) {
          return fpromise::ok(artifact.take_input());
        }
        // If no result was set up, cleanse all bytes except |kCrash|.
        auto pos = as_string(input).find(kCrash);
        if (pos != std::string::npos) {
          std::string cleansed(input.size(), ' ');
          cleansed.replace(pos, kCrashLen, kCrash, 0, kCrashLen);
          return fpromise::ok(Input(cleansed));
        }
        return fpromise::ok(Input());
      })
      .wrap_with(workflow_);
}

ZxPromise<Artifact> FakeRunner::Fuzz() {
  return Run()
      .and_then([this](Artifact& artifact) {
        if (artifact.fuzz_result() != FuzzResult::NO_ERRORS) {
          return fpromise::ok(std::move(artifact));
        }
        // If no result was set up, sequentially increment each byte until it matches |kCrash|.
        char input[kCrashLen + 1] = {0};
        auto max_runs = options_->runs();
        uint32_t runs = 1;
        zx::duration elapsed(0);
        status_.set_running(true);
        status_.set_elapsed(elapsed.to_nsecs());
        status_.set_runs(runs);
        UpdateMonitors(UpdateReason::INIT);
        for (; runs < max_runs || max_runs == 0; ++runs) {
          auto prefix_len = get_prefix_len(input);
          if (prefix_len == kCrashLen) {
            return fpromise::ok(Artifact(FuzzResult::CRASH, Input(kCrash)));
          }
          input[prefix_len]++;
          elapsed += zx::usec(10);
          status_.set_elapsed(elapsed.to_nsecs());
          status_.set_runs(runs);
          if (runs % 10 == 0) {
            UpdateMonitors(UpdateReason::PULSE);
          }
        }
        status_.set_running(false);
        UpdateMonitors(UpdateReason::DONE);
        return fpromise::ok(Artifact(FuzzResult::NO_ERRORS, Input()));
      })
      .wrap_with(workflow_);
}

ZxPromise<> FakeRunner::Merge() {
  return Run()
      .and_then([this](Artifact& artifact) {
        // The fake runner interprets the length of the input prefix that matches |kCrash| as that
        // input's "number of features". This makes merging straightforward, as the input to keep is
        // just the first input of a given prefix length when sorted lexicographically.
        size_t max_prefix_len = 0;
        for (const auto& input : seed_corpus_) {
          auto prefix_len = get_prefix_len(as_string(input));
          if (prefix_len > max_prefix_len) {
            max_prefix_len = prefix_len;
          }
        }
        std::vector<Input> unmerged = std::move(live_corpus_);
        live_corpus_.clear();
        live_corpus_.emplace_back(Input());
        std::sort(unmerged.begin(), unmerged.end());
        for (const auto& input : unmerged) {
          auto prefix_len = get_prefix_len(as_string(input));
          if (prefix_len > max_prefix_len) {
            live_corpus_.emplace_back(input.Duplicate());
            max_prefix_len = prefix_len;
          }
        }
        return fpromise::ok();
      })
      .wrap_with(workflow_);
}

ZxPromise<> FakeRunner::Stop() {
  if (!completer_) {
    Bridge<> bridge;
    completer_ = std::move(bridge.completer);
    consumer_ = std::move(bridge.consumer);
  }
  return workflow_.Stop().inspect(
      [completer = std::move(completer_)](const ZxResult<>& result) mutable {
        completer.complete_ok();
      });
}

Promise<> FakeRunner::AwaitStop() {
  if (!consumer_) {
    Bridge<> bridge;
    completer_ = std::move(bridge.completer);
    consumer_ = std::move(bridge.consumer);
  }
  return consumer_.promise_or(fpromise::error());
}

Status FakeRunner::CollectStatus() { return CopyStatus(status_); }

ZxPromise<Artifact> FakeRunner::Run() {
  return fpromise::make_promise([this]() -> ZxResult<Artifact> {
    if (error_ != ZX_OK) {
      return fpromise::error(error_);
    }
    return fpromise::ok(Artifact(result_, result_input_.Duplicate()));
  });
}

}  // namespace fuzzing
