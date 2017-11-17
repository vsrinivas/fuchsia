// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TEST_BENCHMARK_UPDATE_ENTRY_UPDATE_ENTRY_H_
#define PERIDOT_BIN_LEDGER_TEST_BENCHMARK_UPDATE_ENTRY_UPDATE_ENTRY_H_

#include <memory>

#include "lib/app/cpp/application_context.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/ledger/fidl/ledger.fidl.h"
#include "peridot/bin/ledger/test/data_generator.h"

namespace test {
namespace benchmark {

// Benchmark that measures a performance of Put() operation under the condition
// that it modifies the same entry.
//
// Parameters:
//   --entry-count=<int> the number of entries to be put
//   --transaction-size=<int> the size of a single transaction in number of put
//     operations. If equal to 0, every put operation will be executed
//     individually (implicit transaction).
class UpdateEntryBenchmark {
 public:
  UpdateEntryBenchmark(int entry_count, int transaction_size);

  void Run();

 private:
  void RunSingle(int i, fidl::Array<uint8_t> key);
  void CommitAndRunNext(int i, fidl::Array<uint8_t> key);

  void CommitAndShutDown();
  void ShutDown();

  test::DataGenerator generator_;

  files::ScopedTempDir tmp_dir_;
  std::unique_ptr<app::ApplicationContext> application_context_;
  const int entry_count_;
  const int transaction_size_;
  const int key_size_;
  const int value_size_;

  app::ApplicationControllerPtr application_controller_;
  ledger::PagePtr page_;

  FXL_DISALLOW_COPY_AND_ASSIGN(UpdateEntryBenchmark);
};

}  // namespace benchmark
}  // namespace test

#endif  // PERIDOT_BIN_LEDGER_TEST_BENCHMARK_UPDATE_ENTRY_UPDATE_ENTRY_H_
