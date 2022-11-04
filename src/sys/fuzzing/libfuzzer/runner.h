// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_LIBFUZZER_RUNNER_H_
#define SRC_SYS_FUZZING_LIBFUZZER_RUNNER_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fdio/spawn.h>
#include <lib/fit/function.h>
#include <lib/zx/process.h>
#include <zircon/compiler.h>

#include <memory>
#include <string_view>
#include <vector>

#include <re2/re2.h>

#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/child-process.h"
#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/runner.h"

namespace fuzzing {

using ::fuchsia::fuzzer::Status;

// The concrete implementation of |Runner| for the libfuzzer engine.
class LibFuzzerRunner : public Runner {
 public:
  ~LibFuzzerRunner() override = default;

  // Factory method.
  static RunnerPtr MakePtr(ExecutorPtr executor);

  void set_cmdline(const std::vector<std::string>& cmdline) { cmdline_ = cmdline; }
  void set_verbose(bool verbose) { verbose_ = verbose; }

  // |Runner| methods.
  void OverrideDefaults(Options* options) override;
  __WARN_UNUSED_RESULT zx_status_t AddToCorpus(CorpusType corpus_type, Input input) override;
  std::vector<Input> GetCorpus(CorpusType corpus_type) override;
  __WARN_UNUSED_RESULT zx_status_t ParseDictionary(const Input& input) override;
  Input GetDictionaryAsInput() const override;

  ZxPromise<> Configure(const OptionsPtr& options) override;
  ZxPromise<FuzzResult> Execute(std::vector<Input> input) override;
  ZxPromise<Input> Minimize(Input input) override;
  ZxPromise<Input> Cleanse(Input input) override;
  ZxPromise<Artifact> Fuzz() override;
  ZxPromise<> Merge() override;

  ZxPromise<> Stop() override;

  Status CollectStatus() override;

 private:
  explicit LibFuzzerRunner(ExecutorPtr executor);

  // Construct a set of libFuzzer command-line arguments for the current options and add them to
  // this object's process.
  void AddArgs();

  // Returns a promise that runs a libFuzzer process asynchronously and returns the fuzzing result
  // and the input that caused it.
  ZxPromise<Artifact> RunAsync();

  // Returns a promise that reads the output of the process run by |RunAsync|. The promise will
  // update the fuzzer status and fuzzing result accordingly.
  ZxPromise<> ParseOutput();
  ZxPromise<> ParseStdout();
  ZxPromise<> ParseStderr();

  // Update the list of input files in the live corpus.
  void ReloadLiveCorpus();

  std::vector<std::string> cmdline_;
  OptionsPtr options_;

  // Immutable set of inputs. These will be kept on merge.
  std::vector<std::string> seed_corpus_;

  // Dynamic set of inputs. Inputs may be added during fuzzing, and/or may be removed when merging.
  std::vector<std::string> live_corpus_;

  bool has_dictionary_ = false;
  zx::time start_;

  // If true, echoes libFuzzer's stderr to this component's stderr.
  bool verbose_ = true;

  // If true along with `verbose_`, echoes both the target's stdout and stderr.
  bool print_all_ = false;

  int64_t pid_ = int64_t(-1);
  FuzzResult fuzz_result_ = FuzzResult::NO_ERRORS;

  Status status_;
  std::string result_input_pathname_;

  // Asynchronous process used to run libFuzzer instances.
  ChildProcess process_;
  Barrier barrier_;
  Workflow workflow_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(LibFuzzerRunner);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_LIBFUZZER_RUNNER_H_
