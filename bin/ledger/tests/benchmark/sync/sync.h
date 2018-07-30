// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTS_BENCHMARK_SYNC_SYNC_H_
#define PERIDOT_BIN_LEDGER_TESTS_BENCHMARK_SYNC_SYNC_H_

#include <memory>

#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fit/function.h>
#include <lib/fxl/files/scoped_temp_dir.h>

#include "peridot/bin/cloud_provider_firestore/testing/cloud_provider_factory.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/testing/data_generator.h"
#include "peridot/bin/ledger/testing/page_data_generator.h"
#include "peridot/bin/ledger/testing/sync_params.h"

namespace test {
namespace benchmark {

// Benchmark that measures sync latency between two Ledger instances syncing
// through the cloud. This emulates syncing between devices, as the Ledger
// instances have separate disk storage.
//
// Cloud sync needs to be configured on the device in order for the benchmark to
// run.
//
// Parameters:
//   --change-count=<int> the number of changes to be made to the page (each
//   change is done as transaction and can include several put operations).
//   --value-size=<int> the size of a single value in bytes
//   --entries-per-change=<int> number of entries added in the transaction
//   --refs=(on|off) reference strategy: on to put values as references, off to
//     put them as FIDL arrays.
//   --server-id=<string> the ID of the Firestore instance to use for syncing
//   --api-key=<string> the API key used to access the Firestore instance
//   --credentials-path=<file path> Firestore service account credentials
class SyncBenchmark : public ledger::PageWatcher {
 public:
  SyncBenchmark(async::Loop* loop, size_t change_count, size_t value_size,
                size_t entries_per_change,
                PageDataGenerator::ReferenceStrategy reference_strategy,
                ledger::SyncParams sync_params);

  void Run();

  // ledger::PageWatcher:
  void OnChange(ledger::PageChange page_change,
                ledger::ResultState result_state,
                OnChangeCallback callback) override;

 private:
  void RunSingleChange(size_t change_number);

  void ShutDown();
  fit::closure QuitLoopClosure();

  async::Loop* const loop_;
  test::DataGenerator generator_;
  PageDataGenerator page_data_generator_;
  std::unique_ptr<component::StartupContext> startup_context_;
  cloud_provider_firestore::CloudProviderFactory cloud_provider_factory_;
  const size_t change_count_;
  const size_t value_size_;
  const size_t entries_per_change_;
  const PageDataGenerator::ReferenceStrategy reference_strategy_;
  const std::string user_id_;
  fidl::Binding<ledger::PageWatcher> page_watcher_binding_;
  files::ScopedTempDir alpha_tmp_dir_;
  files::ScopedTempDir beta_tmp_dir_;
  fuchsia::sys::ComponentControllerPtr alpha_controller_;
  fuchsia::sys::ComponentControllerPtr beta_controller_;
  ledger::LedgerPtr alpha_;
  ledger::LedgerPtr beta_;
  ledger::PageId page_id_;
  ledger::PagePtr alpha_page_;
  ledger::PagePtr beta_page_;

  size_t changed_entries_received_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SyncBenchmark);
};

}  // namespace benchmark
}  // namespace test

#endif  // PERIDOT_BIN_LEDGER_TESTS_BENCHMARK_SYNC_SYNC_H_
