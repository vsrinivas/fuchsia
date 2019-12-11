// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <stdlib.h>

#include "src/ledger/bin/app/flags.h"
#include "src/ledger/bin/platform/detached_path.h"
#include "src/ledger/bin/platform/platform.h"
#include "src/ledger/bin/platform/scoped_tmp_location.h"
#include "src/ledger/bin/testing/get_ledger.h"
#include "src/ledger/bin/testing/ledger_memory_usage.h"
#include "src/ledger/bin/testing/run_with_tracing.h"

// A test application meant to be executed as a benchmark. It will complete
// successfully if LedgerMemoryEstimator successfully gets Ledger's memory
// usage.

namespace {

int TryGetMemory(sys::ComponentContext* context, fuchsia::sys::ComponentControllerPtr* controller,
                 int root_fd) {
  ledger::LedgerPtr benchmark_ledger;
  ledger::Status status = ledger::GetLedger(
      context, controller->NewRequest(), nullptr, "", "benchmark_ledger",
      ledger::DetachedPath(root_fd), [] {}, &benchmark_ledger,
      ledger::kTestingGarbageCollectionPolicy);
  if (status != ledger::Status::OK) {
    FXL_LOG(INFO) << "GetLedger failed with status " << fidl::ToUnderlying(status);
    return EXIT_FAILURE;
  }

  ledger::LedgerMemoryEstimator memory_estimator;
  if (!memory_estimator.Init()) {
    FXL_LOG(ERROR) << "MemoryEstimator::Init() failed";
    return EXIT_FAILURE;
  }

  uint64_t memory;
  if (!memory_estimator.GetLedgerMemoryUsage(&memory)) {
    FXL_LOG(ERROR) << "MemoryEstimator::GetLedgerMemoryUsage() failed";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

}  // namespace

int main() {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();
  fuchsia::sys::ComponentControllerPtr controller;
  std::unique_ptr<ledger::Platform> platform = ledger::MakePlatform();
  std::unique_ptr<ledger::ScopedTmpLocation> tmp_location =
      platform->file_system()->CreateScopedTmpLocation();

  int return_code = TryGetMemory(context.get(), &controller, tmp_location->path().root_fd());

  ledger::KillLedgerProcess(&controller);
  loop.Quit();
  return return_code;
}
