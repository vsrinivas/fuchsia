// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTS_BENCHMARK_GET_PAGE_GET_PAGE_H_
#define PERIDOT_BIN_LEDGER_TESTS_BENCHMARK_GET_PAGE_GET_PAGE_H_

#include <vector>

#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/function.h>

#include "lib/app/cpp/startup_context.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/testing/data_generator.h"

namespace test {
namespace benchmark {

// Benchmark that measures the time taken to get a page.
//
// Parameters:
//   --requests-count=<int> number of requests made.
//   --reuse - if this flag is specified, the same id will be used. Otherwise, a
//   new page with a random id is requested every time.
class GetPageBenchmark {
 public:
  GetPageBenchmark(async::Loop* async, size_t requests_count, bool reuse);

  void Run();

 private:
  void RunSingle(size_t request_number);
  void ShutDown();
  fit::closure QuitLoopClosure();

  async::Loop* const loop_;
  files::ScopedTempDir tmp_dir_;
  test::DataGenerator generator_;
  std::unique_ptr<fuchsia::sys::StartupContext> startup_context_;
  const size_t requests_count_;
  const bool reuse_;
  fuchsia::sys::ComponentControllerPtr component_controller_;
  ledger::LedgerPtr ledger_;
  ledger::PageIdPtr page_id_;
  std::vector<ledger::PagePtr> pages_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GetPageBenchmark);
};

}  // namespace benchmark
}  // namespace test

#endif  // PERIDOT_BIN_LEDGER_TESTS_BENCHMARK_GET_PAGE_GET_PAGE_H_
