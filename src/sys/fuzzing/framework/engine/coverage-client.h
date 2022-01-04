// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_FRAMEWORK_ENGINE_COVERAGE_CLIENT_H_
#define SRC_SYS_FUZZING_FRAMEWORK_ENGINE_COVERAGE_CLIENT_H_

#include <fuchsia/fuzzer/cpp/fidl.h>

#include <atomic>
#include <memory>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/dispatcher.h"
#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/shared-memory.h"
#include "src/sys/fuzzing/common/signal-coordinator.h"
#include "src/sys/fuzzing/common/sync-wait.h"
#include "src/sys/fuzzing/framework/engine/corpus.h"

namespace fuzzing {

using fuchsia::fuzzer::CoverageEvent;
using fuchsia::fuzzer::CoverageEventPtr;
using fuchsia::fuzzer::CoverageProvider;
using fuchsia::fuzzer::CoverageProviderPtr;

// This class encapsulates a client of |fuchsia.fuzzer.CoverageProvider|.
class CoverageProviderClient final {
 public:
  CoverageProviderClient();
  ~CoverageProviderClient();

  // Takes ownership of the FIDL request for this client.
  fidl::InterfaceRequest<CoverageProvider> TakeRequest();

  // Sets options. Invokes |fuchsia.fuzzer.CoverageProvider.SetOptions|.
  void Configure(const std::shared_ptr<Options>& options);

  // Sets the |on_event| callback to be invoked on each event. This can only be called once.
  void OnEvent(fit::function<void(CoverageEvent)> on_event);

  // Disconnects the client.
  void Close();

 private:
  std::shared_ptr<Dispatcher> dispatcher_;
  fidl::InterfaceRequest<CoverageProvider> request_;
  CoverageProviderPtr provider_;
  SyncWait sync_;
  std::atomic<bool> closing_ = false;

  std::thread loop_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(CoverageProviderClient);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_ENGINE_COVERAGE_CLIENT_H_
