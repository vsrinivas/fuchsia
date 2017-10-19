// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TEST_BENCHMARK_SYNC_SYNC_H_
#define PERIDOT_BIN_LEDGER_TEST_BENCHMARK_SYNC_SYNC_H_

#include <memory>

#include "lib/app/cpp/application_context.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/ledger/fidl/ledger.fidl.h"
#include "peridot/bin/ledger/test/cloud_provider_firebase_factory.h"
#include "peridot/bin/ledger/test/data_generator.h"
#include "peridot/bin/ledger/test/fake_token_provider.h"

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
//   --entry-count=<int> the number of entries to be put
//   --value-size=<int> the size of a single value in bytes
//   --server-id=<string> the ID of the Firebase instance ot use for syncing
class SyncBenchmark : public ledger::PageWatcher {
 public:
  enum class ReferenceStrategy {
    ON,
    OFF,
    AUTO,
  };

  SyncBenchmark(size_t entry_count,
                size_t value_size,
                ReferenceStrategy reference_strategy,
                std::string server_id);

  void Run();

  // ledger::PageWatcher:
  void OnChange(ledger::PageChangePtr page_change,
                ledger::ResultState result_state,
                const OnChangeCallback& callback) override;

 private:
  void RunSingle(size_t i);

  void Backlog();

  void VerifyBacklog();

  void ShutDown();

  test::DataGenerator generator_;
  std::unique_ptr<app::ApplicationContext> application_context_;
  test::CloudProviderFirebaseFactory cloud_provider_firebase_factory_;
  const size_t entry_count_;
  const size_t value_size_;
  ReferenceStrategy reference_strategy_;
  std::string server_id_;
  fidl::Binding<ledger::PageWatcher> page_watcher_binding_;
  files::ScopedTempDir alpha_tmp_dir_;
  files::ScopedTempDir beta_tmp_dir_;
  files::ScopedTempDir gamma_tmp_dir_;
  app::ApplicationControllerPtr alpha_controller_;
  app::ApplicationControllerPtr beta_controller_;
  app::ApplicationControllerPtr gamma_controller_;
  ledger::LedgerPtr gamma_;
  fidl::Array<uint8_t> page_id_;
  ledger::PagePtr alpha_page_;
  ledger::PagePtr beta_page_;
  ledger::PagePtr gamma_page_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SyncBenchmark);
};

}  // namespace benchmark
}  // namespace test

#endif  // PERIDOT_BIN_LEDGER_TEST_BENCHMARK_SYNC_SYNC_H_
