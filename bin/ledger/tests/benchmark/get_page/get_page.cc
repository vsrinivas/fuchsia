// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/tests/benchmark/get_page/get_page.h"

#include <iostream>

#include <lib/fidl/cpp/clone.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fit/function.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/files/directory.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/strings/string_number_conversions.h>
#include <lib/zx/time.h>
#include <trace/event.h>

#include "peridot/bin/ledger/testing/get_ledger.h"
#include "peridot/bin/ledger/testing/quit_on_error.h"
#include "peridot/bin/ledger/testing/run_with_tracing.h"

namespace {
constexpr fxl::StringView kStoragePath = "/data/benchmark/ledger/get_page";
constexpr fxl::StringView kPageCountFlag = "requests-count";
constexpr fxl::StringView kReuseFlag = "reuse";

void PrintUsage(const char* executable_name) {
  std::cout << "Usage: "
            << executable_name
            // Comment to make clang format not break formatting.
            << " --" << kPageCountFlag << "=<int>"
            << " [--" << kReuseFlag << "]" << std::endl;
}
}  // namespace

namespace test {
namespace benchmark {

GetPageBenchmark::GetPageBenchmark(async::Loop* loop, size_t requests_count,
                                   bool reuse)
    : loop_(loop),
      tmp_dir_(kStoragePath),
      startup_context_(component::StartupContext::CreateFromStartupInfo()),
      requests_count_(requests_count),
      reuse_(reuse) {
  FXL_DCHECK(loop_);
  FXL_DCHECK(requests_count_ > 0);
}

void GetPageBenchmark::Run() {
  test::GetLedger(startup_context_.get(), component_controller_.NewRequest(),
                  nullptr, "get_page", tmp_dir_.path(), QuitLoopClosure(),
                  [this](ledger::Status status, ledger::LedgerPtr ledger) {
                    if (QuitOnError(QuitLoopClosure(), status, "GetLedger")) {
                      return;
                    }
                    ledger_ = std::move(ledger);

                    page_id_ = fidl::MakeOptional(generator_.MakePageId());
                    RunSingle(requests_count_);
                  });
}

void GetPageBenchmark::RunSingle(size_t request_number) {
  if (request_number == 0) {
    ShutDown();
    return;
  }

  TRACE_ASYNC_BEGIN("benchmark", "get page", requests_count_ - request_number);
  ledger::PagePtr page;
  ledger_->GetPage(
      reuse_ ? fidl::Clone(page_id_) : nullptr, page.NewRequest(),
      [this, request_number](ledger::Status status) {
        if (QuitOnError(QuitLoopClosure(), status, "Ledger::GetPage")) {
          return;
        }
        TRACE_ASYNC_END("benchmark", "get page",
                        requests_count_ - request_number);
        RunSingle(request_number - 1);
      });
  pages_.push_back(std::move(page));
}

void GetPageBenchmark::ShutDown() {
  test::KillLedgerProcess(&component_controller_);
  loop_->Quit();
}

fit::closure GetPageBenchmark::QuitLoopClosure() {
  return [this] { loop_->Quit(); };
}

}  // namespace benchmark
}  // namespace test

int main(int argc, const char** argv) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  std::string requests_count_str;
  size_t requests_count;
  if (!command_line.GetOptionValue(kPageCountFlag.ToString(),
                                   &requests_count_str) ||
      !fxl::StringToNumberWithError(requests_count_str, &requests_count) ||
      requests_count == 0) {
    PrintUsage(argv[0]);
    return EXIT_FAILURE;
  }
  bool reuse = command_line.HasOption(kReuseFlag);

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  test::benchmark::GetPageBenchmark app(&loop, requests_count, reuse);

  return test::benchmark::RunWithTracing(&loop, [&app] { app.Run(); });
}
