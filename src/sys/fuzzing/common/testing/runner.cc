// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/testing/runner.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include "src/sys/fuzzing/common/status.h"

namespace fuzzing {

RunnerPtr FakeRunner::MakePtr(ExecutorPtr executor) {
  return RunnerPtr(new FakeRunner(std::move(executor)));
}

FakeRunner::FakeRunner(ExecutorPtr executor) : Runner(executor), workflow_(this) {
  seed_corpus_.push_back(Input());
  live_corpus_.push_back(Input());
}

void FakeRunner::AddDefaults(Options* options) {}

zx_status_t FakeRunner::AddToCorpus(CorpusType corpus_type, Input input) {
  auto* corpus = corpus_type == CorpusType::SEED ? &seed_corpus_ : &live_corpus_;
  corpus->push_back(std::move(input));
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
  return fpromise::make_promise([]() -> ZxResult<> { return fpromise::ok(); }).wrap_with(workflow_);
}

ZxPromise<FuzzResult> FakeRunner::Execute(Input input) {
  return Run()
      .and_then([](const Artifact& artifact) { return fpromise::ok(artifact.fuzz_result()); })
      .wrap_with(workflow_);
}

ZxPromise<Input> FakeRunner::Minimize(Input input) {
  return Run()
      .and_then([](Artifact& artifact) { return fpromise::ok(artifact.take_input()); })
      .wrap_with(workflow_);
}

ZxPromise<Input> FakeRunner::Cleanse(Input input) {
  return Run()
      .and_then([](Artifact& artifact) { return fpromise::ok(artifact.take_input()); })
      .wrap_with(workflow_);
}

ZxPromise<Artifact> FakeRunner::Fuzz() { return Run().wrap_with(workflow_); }

ZxPromise<> FakeRunner::Merge() {
  return Run()
      .and_then([](const Artifact& artifact) { return fpromise::ok(); })
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
