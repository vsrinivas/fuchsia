// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <vector>

#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fit/function.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/strings/string_number_conversions.h>
#include <lib/zx/time.h>
#include <trace/event.h>

#include "peridot/lib/rng/test_random.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/testing/data_generator.h"
#include "src/ledger/bin/testing/get_ledger.h"
#include "src/ledger/bin/testing/get_page_ensure_initialized.h"
#include "src/ledger/bin/testing/quit_on_error.h"
#include "src/ledger/bin/testing/run_with_tracing.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace ledger {
namespace {

constexpr fxl::StringView kBinaryPath =
    "fuchsia-pkg://fuchsia.com/ledger_benchmarks#meta/get_page.cmx";
constexpr fxl::StringView kStoragePath = "/data/benchmark/ledger/get_page";
constexpr fxl::StringView kPageCountFlag = "requests-count";
constexpr fxl::StringView kReuseFlag = "reuse";
constexpr fxl::StringView kWaitForCachedPageFlag = "wait-for-cached-page";

constexpr zx::duration kDuration = zx::msec(500);

void PrintUsage() {
  std::cout << "Usage: trace record "
            << kBinaryPath
            // Comment to make clang format not break formatting.
            << " --" << kPageCountFlag << "=<int>"
            << " [--" << kReuseFlag << "]"
            << " [--" << kWaitForCachedPageFlag << "]" << std::endl;
}

// Benchmark that measures the time taken to get a page.
//
// Parameters:
//   --requests-count=<int> number of requests made.
//   --reuse - if this flag is specified, the same id will be used. Otherwise, a
//   new page with a random id is requested every time.
//   --wait_for_cached_page - if this flag is specified, the benchmark will wait
//   for a sufficient amount of time before each page request, to allow Ledger
//   to precache an empty new page.
class GetPageBenchmark {
 public:
  GetPageBenchmark(async::Loop* loop,
                   std::unique_ptr<component::StartupContext> startup_context,
                   size_t requests_count, bool reuse,
                   bool wait_for_cached_page);

  void Run();

 private:
  void RunSingle(size_t request_number);
  void ShutDown();
  fit::closure QuitLoopClosure();

  async::Loop* const loop_;
  rng::TestRandom random_;
  files::ScopedTempDir tmp_dir_;
  DataGenerator generator_;
  std::unique_ptr<component::StartupContext> startup_context_;
  const size_t requests_count_;
  const bool reuse_;
  const bool wait_for_cached_page_;
  fuchsia::sys::ComponentControllerPtr component_controller_;
  LedgerPtr ledger_;
  PageIdPtr page_id_;
  std::vector<PagePtr> pages_;
  bool get_page_called_ = false;
  bool get_page_id_called_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(GetPageBenchmark);
};

GetPageBenchmark::GetPageBenchmark(
    async::Loop* loop,
    std::unique_ptr<component::StartupContext> startup_context,
    size_t requests_count, bool reuse, bool wait_for_cached_page)
    : loop_(loop),
      random_(0),
      tmp_dir_(kStoragePath),
      generator_(&random_),
      startup_context_(std::move(startup_context)),
      requests_count_(requests_count),
      reuse_(reuse),
      wait_for_cached_page_(wait_for_cached_page) {
  FXL_DCHECK(loop_);
  FXL_DCHECK(requests_count_ > 0);
}

void GetPageBenchmark::Run() {
  Status status = GetLedger(
      startup_context_.get(), component_controller_.NewRequest(), nullptr, "",
      "get_page", DetachedPath(tmp_dir_.path()), QuitLoopClosure(), &ledger_);

  if (QuitOnError(QuitLoopClosure(), status, "GetLedger")) {
    return;
  }

  page_id_ = fidl::MakeOptional(generator_.MakePageId());
  RunSingle(requests_count_);
}

void GetPageBenchmark::RunSingle(size_t request_number) {
  if (request_number == 0) {
    ShutDown();
    return;
  }
  if (wait_for_cached_page_) {
    // Wait before each page request, so that a pre-cached page is ready.
    zx_nanosleep(zx_deadline_after(kDuration.get()));
  }
  TRACE_ASYNC_BEGIN("benchmark", "get page", requests_count_ - request_number);
  PagePtr page;

  get_page_called_ = false;
  get_page_id_called_ = false;
  ledger_->GetPage(
      reuse_ ? fidl::Clone(page_id_) : nullptr, page.NewRequest(),
      [this, request_number](Status status) {
        if (QuitOnError(QuitLoopClosure(), status, "Ledger::GetPage")) {
          return;
        }
        TRACE_ASYNC_END("benchmark", "get page",
                        requests_count_ - request_number);
        get_page_called_ = true;
        if (get_page_id_called_) {
          // Wait for both GetPage and GetId to do the following run.
          RunSingle(request_number - 1);
        }
      });

  TRACE_ASYNC_BEGIN("benchmark", "get page id",
                    requests_count_ - request_number);
  // Request the page id before the GetPage callback is called.
  page->GetId([this, request_number](PageId found_page_id) {
    TRACE_ASYNC_END("benchmark", "get page id",
                    requests_count_ - request_number);
    get_page_id_called_ = true;
    if (get_page_called_) {
      // Wait for both GetPage and GetId to do the following run.
      RunSingle(request_number - 1);
    }
  });

  pages_.push_back(std::move(page));
}

void GetPageBenchmark::ShutDown() {
  KillLedgerProcess(&component_controller_);
  loop_->Quit();
}

fit::closure GetPageBenchmark::QuitLoopClosure() {
  return [this] { loop_->Quit(); };
}

int Main(int argc, const char** argv) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto startup_context = component::StartupContext::CreateFromStartupInfo();

  std::string requests_count_str;
  size_t requests_count;
  if (!command_line.GetOptionValue(kPageCountFlag.ToString(),
                                   &requests_count_str) ||
      !fxl::StringToNumberWithError(requests_count_str, &requests_count) ||
      requests_count == 0) {
    PrintUsage();
    return EXIT_FAILURE;
  }
  bool reuse = command_line.HasOption(kReuseFlag);
  bool wait_for_cached_page = command_line.HasOption(kWaitForCachedPageFlag);

  GetPageBenchmark app(&loop, std::move(startup_context), requests_count, reuse,
                       wait_for_cached_page);

  return RunWithTracing(&loop, [&app] { app.Run(); });
}

}  // namespace
}  // namespace ledger

int main(int argc, const char** argv) { return ledger::Main(argc, argv); }
