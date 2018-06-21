// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTS_BENCHMARK_CONVERGENCE_CONVERGENCE_H_
#define PERIDOT_BIN_LEDGER_TESTS_BENCHMARK_CONVERGENCE_CONVERGENCE_H_

#include <memory>
#include <set>
#include <vector>

#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/function.h>

#include "lib/app/cpp/startup_context.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/testing/cloud_provider_firebase_factory.h"
#include "peridot/bin/ledger/testing/data_generator.h"

namespace test {
namespace benchmark {

// Benchmark that measures the time it takes to sync and reconcile concurrent
// writes.
//
// In this scenario there are specified number of (emulated) devices. At each
// step, every device makes a concurrent write, and we measure the time until
// all the changes are visible to all devices.
//
// Parameters:
//   --entry-count=<int> the number of entries to be put by each device
//   --value-size=<int> the size of a single value in bytes
//   --device-count=<int> number of devices writing to the same page
//   --server-id=<string> the ID of the Firebase instance to use for syncing
class ConvergenceBenchmark : public ledger::PageWatcher {
 public:
  ConvergenceBenchmark(async::Loop* loop, int entry_count, int value_size,
                       int device_count, std::string server_id);

  void Run();

  // ledger::PageWatcher:
  void OnChange(ledger::PageChange page_change,
                ledger::ResultState result_state,
                OnChangeCallback callback) override;

 private:
  struct DeviceContext;

  void Start(int step);

  void ShutDown();
  fit::closure QuitLoopClosure();

  async::Loop* const loop_;
  test::DataGenerator generator_;
  std::unique_ptr<fuchsia::sys::StartupContext> startup_context_;
  test::CloudProviderFirebaseFactory cloud_provider_firebase_factory_;
  const int entry_count_;
  const int value_size_;
  const int device_count_;
  std::string server_id_;
  // Track all Ledger instances running for this test and allow to interact with
  // it.
  std::vector<std::unique_ptr<DeviceContext>> devices_;
  ledger::PageId page_id_;
  std::multiset<std::string> remaining_keys_;
  int current_step_ = -1;

  FXL_DISALLOW_COPY_AND_ASSIGN(ConvergenceBenchmark);
};

}  // namespace benchmark
}  // namespace test

#endif  // PERIDOT_BIN_LEDGER_TESTS_BENCHMARK_CONVERGENCE_CONVERGENCE_H_
