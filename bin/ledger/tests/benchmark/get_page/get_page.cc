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
  std::cout << "Usage: trace record "
            << executable_name
            // Comment to make clang format not break formatting.
            << " --" << kPageCountFlag << "=<int>"
            << " [--" << kReuseFlag << "]" << std::endl;
}
}  // namespace

namespace ledger {

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
  GetLedger(startup_context_.get(), component_controller_.NewRequest(), nullptr,
            "get_page", DetachedPath(tmp_dir_.path()), QuitLoopClosure(),
            [this](Status status, LedgerPtr ledger) {
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

}  // namespace ledger

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
  ledger::GetPageBenchmark app(&loop, requests_count, reuse);

  return ledger::RunWithTracing(&loop, [&app] { app.Run(); });
}
