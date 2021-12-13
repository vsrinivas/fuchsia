// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_FRAMEWORK_ENGINE_ADAPTER_CLIENT_H_
#define SRC_SYS_FUZZING_FRAMEWORK_ENGINE_ADAPTER_CLIENT_H_

#include <fuchsia/fuzzer/cpp/fidl.h>

#include <atomic>
#include <memory>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/shared-memory.h"
#include "src/sys/fuzzing/common/signal-coordinator.h"
#include "src/sys/fuzzing/common/sync-wait.h"
#include "src/sys/fuzzing/framework/engine/corpus.h"

namespace fuzzing {

using fuchsia::fuzzer::TargetAdapter;
using fuchsia::fuzzer::TargetAdapterSyncPtr;

// This class encapsulates a client of |fuchsia.fuzzer.TargetAdapter|.
class TargetAdapterClient final {
 public:
  explicit TargetAdapterClient(fidl::InterfaceRequestHandler<TargetAdapter> handler);
  ~TargetAdapterClient();

  bool is_connected() const { return coordinator_.is_valid(); }

  // Adds default values to unspecified options that are needed by objects of this class.
  static void AddDefaults(Options* options);

  // Sets options. The max input size may be increased by |LoadSeedCorpus|.
  void Configure(const std::shared_ptr<Options>& options);

  // Gets the command-line parameters from the target adapter.
  std::vector<std::string> GetParameters();

  // Signals the target adapter to start a fuzzing iteration using the given |test_input|.
  // Automatically calls |fuchsia.fuzzer.TargetAdapter.Connect| if needed. Does nothing if
  // |SetError| has been called without |ClearError|.
  void Start(Input* test_input);

  // Blocks until the target adapter signals a fuzzing iteration is finished, or until |SetError| or
  // |Close| is called.
  void AwaitFinish();

  // Sets and clears the error state of this object, respectively. When in an error state, |Start|
  // will have no effect and |AwaitFinish| will return immediately.
  void SetError();
  void ClearError();

  // Disconnects the adapter.
  void Close();

 private:
  // Connects to the target adapter if needed. Does nothing if already connected.
  void Connect();

  std::shared_ptr<Options> options_;
  fidl::InterfaceRequestHandler<TargetAdapter> handler_;
  TargetAdapterSyncPtr adapter_;
  SignalCoordinator coordinator_;
  SharedMemory test_input_;
  SyncWait sync_;
  std::atomic<bool> error_ = false;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(TargetAdapterClient);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_ENGINE_ADAPTER_CLIENT_H_
