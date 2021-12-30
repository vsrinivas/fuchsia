// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_LIBFUZZER_RUNNER_H_
#define SRC_SYS_FUZZING_LIBFUZZER_RUNNER_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fdio/spawn.h>
#include <lib/fit/function.h>
#include <lib/zx/process.h>

#include <memory>
#include <string_view>

#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/runner.h"

namespace fuzzing {

using ::fuchsia::fuzzer::Status;

// The concrete implementation of |Runner| for the libfuzzer engine.
class LibFuzzerRunner : public Runner {
 public:
  LibFuzzerRunner();
  ~LibFuzzerRunner() override;

  void set_cmdline(const std::vector<std::string>& cmdline) { cmdline_ = cmdline; }
  void set_verbose(bool verbose) { verbose_ = verbose; }

  // |Runner| methods.
  void AddDefaults(Options* options) override;
  zx_status_t AddToCorpus(CorpusType corpus_type, Input input) override;
  Input ReadFromCorpus(CorpusType corpus_type, size_t offset) override;
  zx_status_t ParseDictionary(const Input& input) override;
  Input GetDictionaryAsInput() const override;
  Status CollectStatus() override;

  // Stages of stopping: close sources of new tasks, interrupt the current task, and join it.
  void Close() override { close_.Run(); }
  void Interrupt() override { interrupt_.Run(); }
  void Join() override { join_.Run(); }

 protected:
  void ConfigureImpl(const std::shared_ptr<Options>& options) override;
  zx_status_t SyncExecute(const Input& input) override;
  zx_status_t SyncMinimize(const Input& input) override;
  zx_status_t SyncCleanse(const Input& input) override;
  zx_status_t SyncFuzz() override;
  zx_status_t SyncMerge() override;

  void ClearErrors() override;

  // Creates the list of actions to perform when spawning libFuzzer. Can be overloaded when testing
  // to provide additional functionality (e.g. startup handles).
  virtual std::vector<fdio_spawn_action_t> MakeSpawnActions();

 private:
  // Construct a set of libFuzzer command-line arguments for the current options.
  std::vector<std::string> MakeArgs();

  // Spawn a libFuzzer process and wait for it to complete.
  zx_status_t Spawn(const std::vector<std::string>& args);

  // Interprets the standard error of libFuzzer.
  void ParseStderr();

  // Attempts to interpret the line as containing status information from libFuzzer.
  // Returns true if |line| is status, false otherwise.
  bool ParseStatus(const std::string_view& line);

  // Attempts to interpret the line as containing other information from libFuzzer.
  void ParseMessage(const std::string_view& line);

  // Update the list of input files in the live corpus.
  void ReloadLiveCorpus();

  // Stop-related methods.
  void CloseImpl();
  void InterruptImpl();
  void JoinImpl();

  std::vector<std::string> cmdline_;
  std::shared_ptr<Options> options_;

  // Immutable set of inputs. These will be kept on merge.
  std::vector<std::string> seed_corpus_;

  // Dynamic set of inputs. Inputs may be added during fuzzing, and/or may be removed when merging.
  std::vector<std::string> live_corpus_;

  bool has_dictionary_ = false;
  zx::time start_;

  std::mutex mutex_;
  bool stopping_ FXL_GUARDED_BY(mutex_) = false;

  // The spawned subprocess and its (initially invalid) stdio file descriptors.
  zx::process subprocess_ FXL_GUARDED_BY(mutex_);
  int piped_stdin_ = -1;
  int piped_stderr_ = -1;

  // If true, eachoes the piped stderr to this process's stderr.
  bool verbose_ = true;

  Status status_;
  zx_status_t error_ = ZX_OK;
  std::string result_input_pathname_;
  bool minimized_ = false;

  RunOnce close_;
  RunOnce interrupt_;
  RunOnce join_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(LibFuzzerRunner);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_LIBFUZZER_RUNNER_H_
