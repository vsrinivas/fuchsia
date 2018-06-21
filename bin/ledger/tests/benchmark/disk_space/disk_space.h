// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTS_BENCHMARK_DISK_SPACE_DISK_SPACE_H_
#define PERIDOT_BIN_LEDGER_TESTS_BENCHMARK_DISK_SPACE_DISK_SPACE_H_

#include <memory>

#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/function.h>

#include "lib/app/cpp/startup_context.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/testing/page_data_generator.h"

namespace test {
namespace benchmark {

// Disk space "general usage" benchmark.
// This benchmark is used to capture Ledger disk usage over the set of common
// operations, such as getting a new page, adding several entries to the page,
// modifying the same entry several times.
//
// The emulated scenario is as follows:
// First, |page_count| pages is requested from ledger. Then each page is
// populated with |unique_key_count| unique entries, making |commit_count|
// commits in the process (so if |commit_count| is bigger than
// |unique_key_count|, some entries get overwritten in subsequent commits,
// whereas if |commit_count| is smaller than |unique_key_count|, insertion
// operations get grouped together into the requested number of commits). Each
// entry has a key size of |key_size| and a value size of |value_size|. After
// that, the connection to the ledger is closed and the size of the directory
// used by it is measured and reported using a trace counter event.
//
// Parameters:
//   --page-count=<int> number of pages to be requested.
//   --unique-key-count=<int> number of unique keys contained in each page
//   after population.
//   --commit-count=<int> number of commits made to each page.
//   If this number is smaller than unique-key-count, changes will be bundled
//   into transactions. If it is bigger, some or all of the changes will use the
//   same keys, modifying the value.
//   --key-size=<int> size of a key for each entry.
//   --value-size=<int> size of a value for each entry.
class DiskSpaceBenchmark {
 public:
  DiskSpaceBenchmark(async::Loop* loop, size_t page_count,
                     size_t unique_key_count, size_t commit_count,
                     size_t key_size, size_t value_size);

  void Run();

 private:
  void Populate();
  void ShutDownAndRecord();
  fit::closure QuitLoopClosure();

  async::Loop* const loop_;
  files::ScopedTempDir tmp_dir_;
  test::DataGenerator generator_;
  PageDataGenerator page_data_generator_;
  std::unique_ptr<fuchsia::sys::StartupContext> startup_context_;
  const size_t page_count_;
  const size_t unique_key_count_;
  const size_t commit_count_;
  const size_t key_size_;
  const size_t value_size_;
  fuchsia::sys::ComponentControllerPtr component_controller_;
  ledger::LedgerPtr ledger_;
  std::vector<ledger::PagePtr> pages_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DiskSpaceBenchmark);
};

}  // namespace benchmark
}  // namespace test

#endif  // PERIDOT_BIN_LEDGER_TESTS_BENCHMARK_DISK_SPACE_DISK_SPACE_H_
