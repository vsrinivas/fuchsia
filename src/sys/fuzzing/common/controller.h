// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_CONTROLLER_H_
#define SRC_SYS_FUZZING_COMMON_CONTROLLER_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>

#include <memory>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/runner.h"

namespace fuzzing {

using ::fuchsia::fuzzer::Controller;
using ::fuchsia::fuzzer::CorpusReader;
using ::fuchsia::fuzzer::Monitor;
using ::fuchsia::fuzzer::TargetAdapter;

using CorpusType = ::fuchsia::fuzzer::Corpus;

class ControllerImpl : public Controller {
 public:
  explicit ControllerImpl(ExecutorPtr executor);

  const RunnerPtr& runner() const { return runner_; }

  // Sets and configures the runner used to perform tasks.
  void SetRunner(RunnerPtr runner);

  // Binds the FIDL interface request to this object.
  void Bind(fidl::InterfaceRequest<Controller> request);

  // FIDL methods.
  void Configure(Options options, ConfigureCallback callback) override;
  void GetOptions(GetOptionsCallback callback) override;
  void AddToCorpus(CorpusType corpus, FidlInput input, AddToCorpusCallback callback) override;
  void ReadCorpus(CorpusType corpus, fidl::InterfaceHandle<CorpusReader> reader,
                  ReadCorpusCallback callback) override;
  void WriteDictionary(FidlInput dictionary, WriteDictionaryCallback callback) override;
  void ReadDictionary(ReadDictionaryCallback callback) override;
  void AddMonitor(fidl::InterfaceHandle<Monitor> monitor, AddMonitorCallback callback) override;
  void GetStatus(GetStatusCallback callback) override;
  void GetResults(GetResultsCallback callback) override;

  void Execute(FidlInput fidl_input, ExecuteCallback callback) override;
  void Minimize(FidlInput fidl_input, MinimizeCallback callback) override;
  void Cleanse(FidlInput fidl_input, CleanseCallback callback) override;
  void Fuzz(FuzzCallback callback) override;
  void Merge(MergeCallback callback) override;

  // Cancels any workflow being executed by this object's runner.
  void Stop();

 private:
  // Returns a promise to configure the runner if needed.
  ZxPromise<> Initialize();

  // Sends the "done marker" for long-running workflows as described in `fuchsia.fuzzer.Controller`.
  void Finish();

  fidl::Binding<Controller> binding_;
  ExecutorPtr executor_;
  RunnerPtr runner_;
  OptionsPtr options_;
  bool initialized_ = false;
  Artifact artifact_;
  Scope scope_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(ControllerImpl);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_CONTROLLER_H_
