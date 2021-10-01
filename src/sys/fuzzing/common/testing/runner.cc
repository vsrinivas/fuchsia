// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/testing/runner.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include "src/sys/fuzzing/common/status.h"

namespace fuzzing {

FakeRunner::FakeRunner() {
  seed_corpus_.push_back(Input());
  live_corpus_.push_back(Input());
}

void FakeRunner::AddDefaults(Options* options) {}

void FakeRunner::set_result(Result result) {
  Runner::set_result(result);
  result_ = result;
}

void FakeRunner::set_result_input(const Input& input) {
  Runner::set_result_input(input);
  result_input_ = input.Duplicate();
}

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

void FakeRunner::ConfigureImpl(const std::shared_ptr<Options>& options) {}

zx_status_t FakeRunner::Run() {
  Runner::set_result(result_);
  Runner::set_result_input(result_input_);
  return error_;
}

Status FakeRunner::CollectStatusLocked() { return CopyStatus(status_); }

}  // namespace fuzzing
