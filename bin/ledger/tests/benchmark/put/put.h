// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTS_BENCHMARK_PUT_PUT_H_
#define PERIDOT_BIN_LEDGER_TESTS_BENCHMARK_PUT_PUT_H_

#include <memory>
#include <set>

#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/function.h>

#include "lib/app/cpp/startup_context.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/testing/data_generator.h"
#include "peridot/bin/ledger/testing/page_data_generator.h"

namespace test {
namespace benchmark {

// Benchmark that measures performance of the Put() operation.
//
// Parameters:
//   --entry-count=<int> the number of entries to be put
//   --transaction-size=<int> the size of a single transaction in number of put
//     operations. If equal to 0, no explicit transactions will be made.
//   --key-size=<int> the size of a single key in bytes
//   --value-size=<int> the size of a single value in bytes
//   --refs=(on|off) the reference strategy: on if every value is inserted
//     as a reference, off if every value is inserted as a FIDL array.
//   --update whether operations will update existing entries (put with existing
//     keys and new values)
//   --seed=<int> (optional) the seed for key and value generation
class PutBenchmark : public ledger::PageWatcher {
 public:
  PutBenchmark(async::Loop* loop, int entry_count, int transaction_size,
               int key_size, int value_size, bool update,
               PageDataGenerator::ReferenceStrategy reference_strategy,
               uint64_t seed);

  void Run();

  // ledger::PageWatcher:
  void OnChange(ledger::PageChange page_change,
                ledger::ResultState result_state,
                OnChangeCallback callback) override;

 private:
  // Initilizes the keys to be used in the benchmark. In case the benchmark is
  // on updating entries, it also adds these keys in the ledger with some
  // initial values.
  void InitializeKeys(
      fit::function<void(std::vector<fidl::VectorPtr<uint8_t>>)> on_done);

  void BindWatcher(std::vector<fidl::VectorPtr<uint8_t>> keys);
  void RunSingle(int i, std::vector<fidl::VectorPtr<uint8_t>> keys);
  void CommitAndRunNext(int i, size_t key_number,
                        std::vector<fidl::VectorPtr<uint8_t>> keys);
  void PutEntry(fidl::VectorPtr<uint8_t> key, fidl::VectorPtr<uint8_t> value,
                fit::function<void()> on_done);

  void ShutDown();
  fit::closure QuitLoopClosure();

  async::Loop* const loop_;
  test::DataGenerator generator_;
  PageDataGenerator page_data_generator_;

  files::ScopedTempDir tmp_dir_;
  std::unique_ptr<fuchsia::sys::StartupContext> startup_context_;
  const int entry_count_;
  const int transaction_size_;
  const int key_size_;
  const int value_size_;
  const bool update_;

  fidl::Binding<ledger::PageWatcher> page_watcher_binding_;
  const PageDataGenerator::ReferenceStrategy reference_strategy_;

  fuchsia::sys::ComponentControllerPtr component_controller_;
  ledger::LedgerPtr ledger_;
  ledger::PagePtr page_;
  // Keys that we use to identify a change event. For transaction_size = 1 it
  // contains all the keys, otherwise only the last changed key for each
  // transaction.
  std::set<size_t> keys_to_receive_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PutBenchmark);
};

}  // namespace benchmark
}  // namespace test

#endif  // PERIDOT_BIN_LEDGER_TESTS_BENCHMARK_PUT_PUT_H_
