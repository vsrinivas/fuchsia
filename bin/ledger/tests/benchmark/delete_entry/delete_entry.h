// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTS_BENCHMARK_DELETE_ENTRY_DELETE_ENTRY_H_
#define PERIDOT_BIN_LEDGER_TESTS_BENCHMARK_DELETE_ENTRY_DELETE_ENTRY_H_

#include <memory>

#include <lib/app/cpp/startup_context.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/function.h>
#include <lib/fxl/files/scoped_temp_dir.h>

#include "peridot/bin/ledger/fidl/include/types.h"
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
  DeleteEntryBenchmark(async::Loop* loop, size_t entry_count,
                       size_t transaction_size, size_t key_size,
                       size_t value_size);

  void Run();

 private:
  void Populate();
  void RunSingle(size_t i);
  void CommitAndRunNext(size_t i);
  void ShutDown();
  fit::closure QuitLoopClosure();

  async::Loop* const loop_;
  files::ScopedTempDir tmp_dir_;
  test::DataGenerator generator_;
  PageDataGenerator page_data_generator_;
  std::unique_ptr<fuchsia::sys::StartupContext> startup_context_;
  const size_t entry_count_;
  const size_t transaction_size_;
  const size_t key_size_;
  const size_t value_size_;
  fuchsia::sys::ComponentControllerPtr component_controller_;
  ledger::LedgerPtr ledger_;
  ledger::PagePtr page_;
  std::vector<fidl::VectorPtr<uint8_t>> keys_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DeleteEntryBenchmark);
};

}  // namespace benchmark
}  // namespace test

#endif  // PERIDOT_BIN_LEDGER_TESTS_BENCHMARK_DELETE_ENTRY_DELETE_ENTRY_H_
