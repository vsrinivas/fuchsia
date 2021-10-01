// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_TESTING_RUNNER_H_
#define SRC_SYS_FUZZING_COMMON_TESTING_RUNNER_H_

#include <fuchsia/fuzzer/cpp/fidl.h>

#include <vector>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/runner.h"

namespace fuzzing {

// This class implements |Runner| without actually running anything. For the fuzzing workflows, it
// simply returns whatever results are preloaded by a unit test.
class FakeRunner final : public Runner {
 public:
  FakeRunner();
  ~FakeRunner() override = default;

  static Input valid_dictionary() { return Input("key=\"value\"\n"); }
  static Input invalid_dictionary() { return Input("invalid"); }

  void set_error(zx_status_t error) { error_ = error; }
  void set_status(Status status) { status_ = std::move(status); }

  using Runner::set_result;
  using Runner::set_result_input;

  void AddDefaults(Options* options) override;
  zx_status_t AddToCorpus(CorpusType corpus_type, Input input) override;
  Input ReadFromCorpus(CorpusType corpus_type, size_t offset) const override;
  zx_status_t ParseDictionary(const Input& input) override;
  Input GetDictionaryAsInput() const override;

  using Runner::UpdateMonitors;

 protected:
  void ConfigureImpl(const std::shared_ptr<Options>& options) override;

  zx_status_t SyncExecute(const Input& input) override;
  zx_status_t SyncMinimize(const Input& input) override;
  zx_status_t SyncCleanse(const Input& input) override;
  zx_status_t SyncFuzz() override;
  zx_status_t SyncMerge() override;
  Status CollectStatusLocked() override;

 private:
  zx_status_t error_ = ZX_OK;
  Status status_;
  std::vector<Input> seed_corpus_;
  std::vector<Input> live_corpus_;
  Input dictionary_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(FakeRunner);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_TESTING_RUNNER_H_
