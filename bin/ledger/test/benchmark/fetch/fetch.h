// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TEST_BENCHMARK_FETCH_FETCH_H_
#define PERIDOT_BIN_LEDGER_TEST_BENCHMARK_FETCH_FETCH_H_

#include <memory>
#include <vector>

#include "lib/app/cpp/application_context.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/ledger/fidl/ledger.fidl.h"
#include "peridot/bin/ledger/test/cloud_provider_firebase_factory.h"
#include "peridot/bin/ledger/test/data_generator.h"
#include "peridot/lib/firebase_auth/test/fake_token_provider.h"

namespace test {
namespace benchmark {

// Benchmark that measures time to fetch lazy values from server.
// Parameters:
//   --entry-count=<int> the number of entries to be put
//   --value-size=<int> the size of a single value in bytes
//   --part-size=<int> the size of the part to be read with one Fetch
//   call. If equal to zero, the whole value will be read.
//   --server-id=<string> the ID of the Firebase instance to use for storing
//   values.
class FetchBenchmark : public ledger::SyncWatcher {
 public:
  FetchBenchmark(size_t entry_count,
                 size_t value_size,
                 size_t part_size,
                 std::string server_id);

  void Run();

  // ledger::SyncWatcher:
  void SyncStateChanged(ledger::SyncState download,
                        ledger::SyncState upload,
                        const SyncStateChangedCallback& callback) override;

 private:
  void PutEntry(fidl::Array<uint8_t> key,
                fidl::Array<uint8_t> value,
                std::function<void(ledger::Status)> put_callback);

  void Populate();
  void WaitForWriterUpload();
  void ConnectReader();
  void WaitForReaderDownload();

  void Backlog();
  void VerifyBacklog(ledger::PageSnapshotPtr snapshot,
                     fidl::Array<uint8_t> token);

  void FetchValues(ledger::PageSnapshotPtr snapshot, size_t i);
  void FetchPart(ledger::PageSnapshotPtr snapshot, size_t i, size_t part);

  void ShutDown();

  test::DataGenerator generator_;
  std::unique_ptr<app::ApplicationContext> application_context_;
  test::CloudProviderFirebaseFactory cloud_provider_firebase_factory_;
  fidl::Binding<ledger::SyncWatcher> sync_watcher_binding_;
  const size_t entry_count_;
  const size_t value_size_;
  const size_t part_size_;
  std::string server_id_;
  files::ScopedTempDir writer_tmp_dir_;
  files::ScopedTempDir reader_tmp_dir_;
  app::ApplicationControllerPtr writer_controller_;
  app::ApplicationControllerPtr reader_controller_;
  ledger::LedgerPtr writer_;
  ledger::LedgerPtr reader_;
  fidl::Array<uint8_t> page_id_;
  ledger::PagePtr writer_page_;
  ledger::PagePtr reader_page_;
  std::vector<fidl::Array<uint8_t>> keys_;
  std::function<void(ledger::SyncState, ledger::SyncState)>
      on_sync_state_changed_;
  ledger::SyncState previous_state_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FetchBenchmark);
};

}  // namespace benchmark
}  // namespace test

#endif  // PERIDOT_BIN_LEDGER_TEST_BENCHMARK_FETCH_FETCH_H_
