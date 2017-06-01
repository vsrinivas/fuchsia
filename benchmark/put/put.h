// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_BENCHMARK_PUT_PUT_H_
#define APPS_LEDGER_BENCHMARK_PUT_PUT_H_

#include <memory>

#include "application/lib/app/application_context.h"
#include "apps/ledger/services/public/ledger.fidl.h"
#include "lib/ftl/files/scoped_temp_dir.h"

namespace benchmark {

// Benchmark that measures performance of the Put() operation.
//
// Parameters:
//   --entry-count=<int> the number of entries to be put
//   --transaction-size=<int> the size of a single transaction in number of put
//     operations. If equal to 1, every put operation will be executed
//     individually.
//   --key-size=<int> the size of a single key in bytes
//   --value-size=<int> the size of a single value in bytes
//   --update whether operations will update existing entries (put with existing
//     keys and new values)
class PutBenchmark {
 public:
  PutBenchmark(int entry_count,
               int transaction_size,
               int key_size,
               int value_size,
               bool update);

  void Run();

 private:
  // Initilizes the keys to be used in the benchmark. In case the benchmark is
  // on updating entries, it also adds these keys in the ledger with some
  // initial values.
  void InitializeKeys(
      std::function<void(std::vector<fidl::Array<uint8_t>>)> on_done);
  // Recursively adds entries using all given keys and random values, which are
  // to be updated later in the benchmark.
  void AddInitialEntries(
      int i,
      std::vector<fidl::Array<uint8_t>> keys,
      std::function<void(std::vector<fidl::Array<uint8_t>>)> on_done);

  void RunSingle(int i, std::vector<fidl::Array<uint8_t>> keys);
  void CommitAndRunNext(int i, std::vector<fidl::Array<uint8_t>> keys);

  void CommitAndShutDown();
  void ShutDown();

  files::ScopedTempDir tmp_dir_;
  std::unique_ptr<app::ApplicationContext> application_context_;
  const int entry_count_;
  const int transaction_size_;
  const int key_size_;
  const int value_size_;
  const bool update_;

  app::ApplicationControllerPtr application_controller_;
  ledger::PagePtr page_;

  FTL_DISALLOW_COPY_AND_ASSIGN(PutBenchmark);
};

}  // namespace benchmark

#endif  // APPS_LEDGER_BENCHMARK_PUT_PUT_H_
