// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTS_BENCHMARK_BACKLOG_BACKLOG_H_
#define PERIDOT_BIN_LEDGER_TESTS_BENCHMARK_BACKLOG_BACKLOG_H_

#include <memory>
#include <vector>

#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/function.h>

#include "lib/app/cpp/startup_context.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/testing/cloud_provider_firebase_factory.h"
#include "peridot/bin/ledger/testing/data_generator.h"
#include "peridot/bin/ledger/testing/page_data_generator.h"
#include "peridot/lib/firebase_auth/testing/fake_token_provider.h"

namespace test {
namespace benchmark {

// Benchmark that measures time taken by a page connection to upload all local
// changes to the cloud; and for another connection to the same page to download
// all these changes.
//
// In contrast to the sync benchmark, backlog benchmark initiates the second
// connection only after the first one has uploaded all changes. It is designed
// to model the situation of adding new device instead of continuous
// synchronisation.
//
// Cloud sync needs to be configured on the device in order for the benchmark to
// run.
//
// Parameters:
//   --unique-key-count=<int> the number of unique keys to populate the page
//   with.
//   --value-size=<int> the size of values to populate the page with.
//   --commit-count=<int> the number of commits made to the page.
//   If this number is smaller than unique-key-count, changes will be bundled
//   into transactions. If it is bigger, some or all of the changes will use the
//   same keys, modifying the value.
//   --refs=(on|off) reference strategy: on to put values as references, off to
//     put them as FIDL arrays.
//   --server-id=<string> the ID of the Firebase instance to use for syncing
class BacklogBenchmark : public ledger::SyncWatcher {
 public:
  BacklogBenchmark(async::Loop* loop, size_t unique_key_count,
                   size_t value_size, size_t commit_count,
                   PageDataGenerator::ReferenceStrategy reference_strategy,
                   std::string server_id);

  void Run();

  // ledger::SyncWatcher:
  void SyncStateChanged(ledger::SyncState download, ledger::SyncState upload,
                        SyncStateChangedCallback callback) override;

 private:
  void Populate();
  void WaitForWriterUpload();
  void ConnectReader();
  void WaitForReaderDownload();

  void GetReaderSnapshot();
  void GetEntriesStep(std::unique_ptr<ledger::Token> token,
                      size_t entries_left);
  void CheckStatusAndGetMore(ledger::Status status, size_t entries_read,
                             std::unique_ptr<ledger::Token> next_token);

  void RecordDirectorySize(const std::string& event_name,
                           const std::string& path);
  void ShutDown();
  fit::closure QuitLoopClosure();

  async::Loop* const loop_;
  DataGenerator generator_;
  PageDataGenerator page_data_generator_;
  std::unique_ptr<fuchsia::sys::StartupContext> startup_context_;
  CloudProviderFirebaseFactory cloud_provider_firebase_factory_;
  fidl::Binding<ledger::SyncWatcher> sync_watcher_binding_;
  const size_t unique_key_count_;
  const size_t value_size_;
  const size_t commit_count_;
  const PageDataGenerator::ReferenceStrategy reference_strategy_;
  const std::string server_id_;
  files::ScopedTempDir writer_tmp_dir_;
  files::ScopedTempDir reader_tmp_dir_;
  fuchsia::sys::ComponentControllerPtr writer_controller_;
  fuchsia::sys::ComponentControllerPtr reader_controller_;
  ledger::LedgerPtr writer_;
  ledger::LedgerPtr reader_;
  ledger::PageId page_id_;
  ledger::PagePtr writer_page_;
  ledger::PagePtr reader_page_;
  ledger::PageSnapshotPtr reader_snapshot_;
  fit::function<void(ledger::SyncState, ledger::SyncState)>
      on_sync_state_changed_;
  ledger::SyncState previous_state_;
  bool done_writing_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BacklogBenchmark);
};

}  // namespace benchmark
}  // namespace test

#endif  // PERIDOT_BIN_LEDGER_TESTS_BENCHMARK_BACKLOG_BACKLOG_H_
