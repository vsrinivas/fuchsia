// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_TESTING_RUNNER_H_
#define SRC_SYS_FUZZING_TESTING_RUNNER_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/zx/time.h>
#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <random>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/runner.h"
#include "src/sys/fuzzing/common/status.h"

namespace fuzzing {

using ::fuchsia::fuzzer::Status;
using CorpusType = ::fuchsia::fuzzer::Corpus;

// This class provides a simple implementation of |Runner| with a builtin target. The target reports
// a crash if given an input that includes "CRASH".
class SimpleFixedRunner final : public Runner {
 public:
  SimpleFixedRunner();
  ~SimpleFixedRunner() override = default;

  // See ../common/runner.h.
  void AddDefaults(Options* options) override;
  zx_status_t AddToCorpus(CorpusType corpus_type, Input input) override;
  Input ReadFromCorpus(CorpusType corpus_type, size_t offset) override;
  zx_status_t ParseDictionary(const Input& input) override;
  Input GetDictionaryAsInput() const override;

 protected:
  void ConfigureImpl(const std::shared_ptr<Options>& options) override;
  zx_status_t SyncExecute(const Input& input) override;
  zx_status_t SyncMinimize(const Input& input) override;
  zx_status_t SyncCleanse(const Input& input) override;
  zx_status_t SyncFuzz() override;
  zx_status_t SyncMerge() override;
  void Interrupt() override {}
  Status CollectStatus() override;

 private:
  FuzzResult TestOne(const Input& input);
  size_t Measure(const Input& input, bool accumulate);

  std::shared_ptr<Options> options_;
  std::vector<Input> seed_corpus_;
  std::vector<Input> live_corpus_;
  Input dictionary_;
  std::minstd_rand prng_;
  uint32_t run_ = 0;
  size_t matched_ = 0;
  zx::time start_;
  zx::time pulse_at_;
  Status status_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(SimpleFixedRunner);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_TESTING_RUNNER_H_
