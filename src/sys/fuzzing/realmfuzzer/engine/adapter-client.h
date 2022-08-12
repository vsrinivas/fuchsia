// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_REALMFUZZER_ENGINE_ADAPTER_CLIENT_H_
#define SRC_SYS_FUZZING_REALMFUZZER_ENGINE_ADAPTER_CLIENT_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fidl/cpp/interface_request.h>

#include <string>
#include <vector>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/async-eventpair.h"
#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/shared-memory.h"
#include "src/sys/fuzzing/realmfuzzer/engine/corpus.h"

namespace fuzzing {

using fuchsia::fuzzer::TargetAdapter;
using fuchsia::fuzzer::TargetAdapterSyncPtr;

// This class encapsulates a client of |fuchsia.fuzzer.TargetAdapter|.
class TargetAdapterClient final {
 public:
  explicit TargetAdapterClient(ExecutorPtr executor);
  ~TargetAdapterClient() = default;

  // Sets options. The max input size may be increased by |LoadSeedCorpus|.
  void Configure(const OptionsPtr& options);

  // FIDL binding methods.
  // TODO(fxbug.dev/92490): This handler will become asynchronous and return a promise. The alias
  // will be modified in the same change as the test fixtures.
  using RequestHandler = fidl::InterfaceRequestHandler<TargetAdapter>;
  void set_handler(RequestHandler handler) { handler_ = std::move(handler); }

  // Gets the command-line parameters from the target adapter.
  Promise<std::vector<std::string>> GetParameters();

  // Filters everything but the seed corpus directories from a list of |parameters|.
  std::vector<std::string> GetSeedCorpusDirectories(const std::vector<std::string>& parameters);

  // Signals the target adapter to start a fuzzing iteration using the given |test_input|.
  // Returns a promise that completes when the target adapter indicates the fuzzing run is complete.
  Promise<> TestOneInput(const Input& test_input);

  // Disconnects the adapter.
  void Disconnect();

 private:
  // Connects to the target adapter if needed and returns a promise that completes when connected.
  Promise<> Connect();

  fidl::InterfacePtr<TargetAdapter> ptr_;
  ExecutorPtr executor_;
  Scope scope_;

  RequestHandler handler_;
  AsyncEventPair eventpair_;
  SharedMemory test_input_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(TargetAdapterClient);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_REALMFUZZER_ENGINE_ADAPTER_CLIENT_H_
