// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/benchmark/put/put.h"

#include <iostream>

#include "apps/ledger/benchmark/lib/convert.h"
#include "apps/ledger/benchmark/lib/data.h"
#include "apps/ledger/benchmark/lib/get_ledger.h"
#include "apps/ledger/benchmark/lib/logging.h"
#include "apps/tracing/lib/trace/event.h"
#include "apps/tracing/lib/trace/provider.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_number_conversions.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {
constexpr ftl::StringView kStoragePath = "/data/benchmark/ledger/put";
constexpr ftl::StringView kEntryCountFlag = "entry-count";
constexpr ftl::StringView kValueSizeFlag = "value-size";
constexpr ftl::StringView kTransactionFlag = "transaction";

void PrintUsage(const char* executable_name) {
  std::cout << "Usage: " << executable_name << " --" << kEntryCountFlag
            << "=<int> --" << kValueSizeFlag << "=<int>" << std::endl;
}

}  // namespace

namespace benchmark {

PutBenchmark::PutBenchmark(int entry_count, int value_size, bool transaction)
    : tmp_dir_(kStoragePath),
      application_context_(app::ApplicationContext::CreateFromStartupInfo()),
      entry_count_(entry_count),
      value_size_(value_size),
      transaction_(transaction) {
  FTL_DCHECK(entry_count > 0);
  FTL_DCHECK(value_size > 0);
  tracing::InitializeTracer(application_context_.get(),
                            {"benchmark_ledger_put"});
}

void PutBenchmark::Run() {
  ledger::LedgerPtr ledger =
      benchmark::GetLedger(application_context_.get(), &ledger_controller_,
                           "put", tmp_dir_.path(), false, "");
  benchmark::GetPageEnsureInitialized(
      ledger.get(), nullptr, [this](ledger::PagePtr page, auto id) {
        page_ = std::move(page);
        if (transaction_) {
          page_->StartTransaction([this](ledger::Status status) {
            if (benchmark::QuitOnError(status, "Page::StartTransaction")) {
              return;
            }
            TRACE_ASYNC_BEGIN("benchmark", "transaction", 0);
            RunSingle(0, entry_count_);
          });
        } else {
          RunSingle(0, entry_count_);
        }
      });
}

void PutBenchmark::RunSingle(int i, int count) {
  if (i == count) {
    if (transaction_) {
      CommitAndMeasure();
    } else {
      ShutDown();
    }
    return;
  }

  fidl::Array<uint8_t> key = benchmark::MakeKey(i);
  fidl::Array<uint8_t> value = benchmark::MakeValue(value_size_);
  TRACE_ASYNC_BEGIN("benchmark", "put", i);
  page_->Put(std::move(key), std::move(value),
             [this, i, count](ledger::Status status) {
               if (benchmark::QuitOnError(status, "Page::Put")) {
                 return;
               }
               TRACE_ASYNC_END("benchmark", "put", i);
               RunSingle(i + 1, count);
             });
}

void PutBenchmark::CommitAndMeasure() {
  TRACE_ASYNC_BEGIN("benchmark", "commit", 0);
  page_->Commit([this](ledger::Status status) {
    if (benchmark::QuitOnError(status, "Page::Commit")) {
      return;
    }
    TRACE_ASYNC_END("benchmark", "commit", 0);
    TRACE_ASYNC_END("benchmark", "transaction", 0);
    ShutDown();
  });
}

void PutBenchmark::ShutDown() {
  // Shut down the Ledger process first as it relies on |tmp_dir_| storage.
  ledger_controller_->Kill();
  ledger_controller_.WaitForIncomingResponseWithTimeout(
      ftl::TimeDelta::FromSeconds(5));
  mtl::MessageLoop::GetCurrent()->PostQuitTask();
}
}  // namespace benchmark

int main(int argc, const char** argv) {
  ftl::CommandLine command_line = ftl::CommandLineFromArgcArgv(argc, argv);

  std::string entry_count_str;
  int entry_count;
  std::string value_size_str;
  int value_size;
  bool transaction = command_line.HasOption(kTransactionFlag.ToString());
  if (!command_line.GetOptionValue(kEntryCountFlag.ToString(),
                                   &entry_count_str) ||
      !ftl::StringToNumberWithError(entry_count_str, &entry_count) ||
      entry_count <= 0 ||
      !command_line.GetOptionValue(kValueSizeFlag.ToString(),
                                   &value_size_str) ||
      !ftl::StringToNumberWithError(value_size_str, &value_size) ||
      value_size <= 0) {
    PrintUsage(argv[0]);
    return -1;
  }

  mtl::MessageLoop loop;
  benchmark::PutBenchmark app(entry_count, value_size, transaction);
  loop.task_runner()->PostTask([&app] { app.Run(); });
  loop.Run();
  return 0;
}
