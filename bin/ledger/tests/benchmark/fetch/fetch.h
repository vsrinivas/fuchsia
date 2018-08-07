// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTS_BENCHMARK_FETCH_FETCH_H_
#define PERIDOT_BIN_LEDGER_TESTS_BENCHMARK_FETCH_FETCH_H_

#include <memory>
#include <vector>

#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fit/function.h>
#include <lib/fxl/files/scoped_temp_dir.h>

#include "peridot/bin/cloud_provider_firestore/testing/cloud_provider_factory.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/testing/data_generator.h"
#include "peridot/bin/ledger/testing/page_data_generator.h"
#include "peridot/bin/ledger/testing/sync_params.h"

namespace ledger {

// Benchmark that measures time to fetch lazy values from server.
// Parameters:
//   --entry-count=<int> the number of entries to be put
//   --value-size=<int> the size of a single value in bytes
//   --part-size=<int> the size of the part to be read with one Fetch
//   call. If equal to zero, the whole value will be read.
//   --server-id=<string> the ID of the Firestore instance to use for syncing
//   --api-key=<string> the API key used to access the Firestore instance
//   --credentials-path=<file path> Firestore service account credentials
class FetchBenchmark : public SyncWatcher {
 public:
  FetchBenchmark(async::Loop* loop, size_t entry_count, size_t value_size,
                 size_t part_size, SyncParams sync_params);

  void Run();

  // SyncWatcher:
  void SyncStateChanged(SyncState download, SyncState upload,
                        SyncStateChangedCallback callback) override;

 private:
  void Populate();
  void WaitForWriterUpload();
  void ConnectReader();
  void WaitForReaderDownload();

  void FetchValues(PageSnapshotPtr snapshot, size_t i);
  void FetchPart(PageSnapshotPtr snapshot, size_t i, size_t part);

  void ShutDown();
  fit::closure QuitLoopClosure();

  async::Loop* const loop_;
  DataGenerator generator_;
  PageDataGenerator page_data_generator_;
  std::unique_ptr<component::StartupContext> startup_context_;
  cloud_provider_firestore::CloudProviderFactory cloud_provider_factory_;
  fidl::Binding<SyncWatcher> sync_watcher_binding_;
  const size_t entry_count_;
  const size_t value_size_;
  const size_t part_size_;
  const std::string user_id_;
  files::ScopedTempDir writer_tmp_dir_;
  files::ScopedTempDir reader_tmp_dir_;
  fuchsia::sys::ComponentControllerPtr writer_controller_;
  fuchsia::sys::ComponentControllerPtr reader_controller_;
  LedgerPtr writer_;
  LedgerPtr reader_;
  PageId page_id_;
  PagePtr writer_page_;
  PagePtr reader_page_;
  std::vector<fidl::VectorPtr<uint8_t>> keys_;
  fit::function<void(SyncState, SyncState)> on_sync_state_changed_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FetchBenchmark);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_TESTS_BENCHMARK_FETCH_FETCH_H_
