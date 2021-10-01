// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_CONTROLLER_H_
#define SRC_SYS_FUZZING_COMMON_CONTROLLER_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>

#include <atomic>
#include <memory>
#include <mutex>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/binding.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/response.h"
#include "src/sys/fuzzing/common/runner.h"
#include "src/sys/fuzzing/common/transceiver.h"

namespace fuzzing {

using ::fuchsia::fuzzer::Controller;
using ::fuchsia::fuzzer::CorpusReader;
using ::fuchsia::fuzzer::CorpusReaderPtr;
using ::fuchsia::fuzzer::Monitor;
using ::fuchsia::fuzzer::ProcessProxy;
using ::fuchsia::fuzzer::TargetAdapter;

using CorpusType = ::fuchsia::fuzzer::Corpus;
using FidlInput = ::fuchsia::fuzzer::Input;

class ControllerImpl : public Controller {
 public:
  ControllerImpl();
  virtual ~ControllerImpl() = default;

  // Sets the runner used to perform tasks.
  void SetRunner(const std::shared_ptr<Runner>& runner);

  // Binds the FIDL interface request to this object.
  void Bind(fidl::InterfaceRequest<Controller> request, async_dispatcher_t* dispatcher);

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

 private:
  // Adds defaults for unset options.
  void AddDefaults();

  // Factory method for making FIDL responses.
  template <typename Callback>
  Response NewResponse(Callback callback) {
    Response response;
    response.set_dispatcher(binding_.dispatcher());
    response.set_transceiver(&transceiver_);
    response.set_callback(std::move(callback));
    return response;
  }

  // Asynchronously receives a |fidl_input| via the transceiver before invoking the |callback| and
  // using it to send the |response|.
  void ReceiveAndThen(FidlInput fidl_input, Response response,
                      fit::function<void(Input, Response)> callback);

  Binding<Controller> binding_;
  std::shared_ptr<Options> options_;
  std::shared_ptr<Runner> runner_;
  Transceiver transceiver_;
  CorpusReaderPtr reader_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(ControllerImpl);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_CONTROLLER_H_
