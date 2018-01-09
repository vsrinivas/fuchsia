// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTS_BENCHMARK_DELETE_ENTRY_DELETE_ENTRY_H_
#define PERIDOT_BIN_LEDGER_TESTS_BENCHMARK_DELETE_ENTRY_DELETE_ENTRY_H_

#include <memory>

#include "lib/app/cpp/application_context.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/ledger/fidl/ledger.fidl.h"
#include "peridot/bin/ledger/testing/data_generator.h"
#include "peridot/bin/ledger/testing/page_data_generator.h"

namespace test {
namespace benchmark {

// Benchmark that measures the time taken to delete an entry from a page.
//
// Parameters:
//   --entry-count=<int> the number of entries to be put and deleted
//   --transaction_size=<int> number of delete operations in each transaction. 0
//     means no explicit transactions.
//   --key-size=<int> size of the keys for the entries
//   --value-size=<int> the size of a single value in bytes
class DeleteEntryBenchmark {
 public:
  DeleteEntryBenchmark(size_t entry_count,
                       size_t transaction_size,
                       size_t key_size,
                       size_t value_size);

  void Run();

 private:
  void Populate();
  void RunSingle(size_t i);
  void CommitAndRunNext(size_t i);
  void ShutDown();

  files::ScopedTempDir tmp_dir_;
  test::DataGenerator generator_;
  PageDataGenerator page_data_generator_;
  std::unique_ptr<app::ApplicationContext> application_context_;
  const size_t entry_count_;
  const size_t transaction_size_;
  const size_t key_size_;
  const size_t value_size_;
  app::ApplicationControllerPtr application_controller_;
  ledger::PagePtr page_;
  std::vector<fidl::Array<uint8_t>> keys_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DeleteEntryBenchmark);
};

}  // namespace benchmark
}  // namespace test

#endif  // PERIDOT_BIN_LEDGER_TESTS_BENCHMARK_DELETE_ENTRY_DELETE_ENTRY_H_
