// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/tests/benchmark/update_entry/update_entry.h"

#include <iostream>

#include <lib/zx/time.h>
#include <trace/event.h>

#include "lib/fidl/cpp/clone.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/files/directory.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "peridot/bin/ledger/testing/get_ledger.h"
#include "peridot/bin/ledger/testing/quit_on_error.h"
#include "peridot/bin/ledger/testing/run_with_tracing.h"
#include "peridot/lib/convert/convert.h"

namespace {

constexpr fxl::StringView kStoragePath = "/data/benchmark/ledger/update_entry";
constexpr fxl::StringView kEntryCountFlag = "entry-count";
constexpr fxl::StringView kValueSizeFlag = "value-size";
constexpr fxl::StringView kTransactionSizeFlag = "transaction-size";

const int kKeySize = 100;

void PrintUsage(const char* executable_name) {
  std::cout << "Usage: " << executable_name << " --" << kEntryCountFlag
            << "=<int> --" << kValueSizeFlag << "=<int> --"
            << kTransactionSizeFlag << "=<int>" << std::endl;
}

}  // namespace

namespace test {
namespace benchmark {

UpdateEntryBenchmark::UpdateEntryBenchmark(async::Loop* loop, int entry_count,
                                           int value_size, int transaction_size)
    : loop_(loop),
      tmp_dir_(kStoragePath),
      startup_context_(fuchsia::sys::StartupContext::CreateFromStartupInfo()),
      entry_count_(entry_count),
      transaction_size_(transaction_size),
      key_size_(kKeySize),
      value_size_(value_size) {
  FXL_DCHECK(loop_);
  FXL_DCHECK(entry_count_ > 0);
  FXL_DCHECK(value_size_ > 0);
  FXL_DCHECK(key_size_ > 0);
  FXL_DCHECK(transaction_size_ >= 0);
}

void UpdateEntryBenchmark::Run() {
  FXL_LOG(INFO) << "--entry-count=" << entry_count_
                << " --transaction-size=" << transaction_size_;
  test::GetLedger(
      startup_context_.get(), component_controller_.NewRequest(), nullptr,
      "update_entry", tmp_dir_.path(), QuitLoopClosure(),
      [this](ledger::Status status, ledger::LedgerPtr ledger) {
        if (QuitOnError(QuitLoopClosure(), status, "GetLedger")) {
          return;
        }
        ledger_ = std::move(ledger);
        test::GetPageEnsureInitialized(
            &ledger_, nullptr, QuitLoopClosure(),
            [this](ledger::Status status, ledger::PagePtr page,
                   ledger::PageId id) {
              if (QuitOnError(QuitLoopClosure(), status,
                              "GetPageEnsureInitialized")) {
                return;
              }
              page_ = std::move(page);
              fidl::VectorPtr<uint8_t> key = generator_.MakeKey(0, key_size_);
              if (transaction_size_ > 0) {
                page_->StartTransaction(
                    fxl::MakeCopyable([this, key = std::move(key)](
                                          ledger::Status status) mutable {
                      if (benchmark::QuitOnError(QuitLoopClosure(), status,
                                                 "Page::StartTransaction")) {
                        return;
                      }
                      TRACE_ASYNC_BEGIN("benchmark", "transaction", 0);
                      RunSingle(0, std::move(key));
                    }));
              } else {
                RunSingle(0, std::move(key));
              }
            });
      });
}

void UpdateEntryBenchmark::RunSingle(int i, fidl::VectorPtr<uint8_t> key) {
  if (i == entry_count_) {
    if (transaction_size_ > 0) {
      CommitAndShutDown();
    } else {
      ShutDown();
    }
    return;
  }

  fidl::VectorPtr<uint8_t> value = generator_.MakeValue(value_size_);
  TRACE_ASYNC_BEGIN("benchmark", "put", i);
  page_->Put(fidl::Clone(key), std::move(value),
             fxl::MakeCopyable([this, i, key = std::move(key)](
                                   ledger::Status status) mutable {
               if (benchmark::QuitOnError(QuitLoopClosure(), status,
                                          "Page::Put")) {
                 return;
               }
               TRACE_ASYNC_END("benchmark", "put", i);
               if (transaction_size_ > 0 &&
                   i % transaction_size_ == transaction_size_ - 1) {
                 CommitAndRunNext(i, std::move(key));
               } else {
                 RunSingle(i + 1, std::move(key));
               }
             }));
}

void UpdateEntryBenchmark::CommitAndRunNext(int i,
                                            fidl::VectorPtr<uint8_t> key) {
  TRACE_ASYNC_BEGIN("benchmark", "commit", i / transaction_size_);
  page_->Commit(fxl::MakeCopyable([this, i, key = std::move(key)](
                                      ledger::Status status) mutable {
    if (benchmark::QuitOnError(QuitLoopClosure(), status, "Page::Commit")) {
      return;
    }
    TRACE_ASYNC_END("benchmark", "commit", i / transaction_size_);
    TRACE_ASYNC_END("benchmark", "transaction", i / transaction_size_);

    page_->StartTransaction(fxl::MakeCopyable(
        [this, i = i + 1, key = std::move(key)](ledger::Status status) mutable {
          if (benchmark::QuitOnError(QuitLoopClosure(), status,
                                     "Page::StartTransaction")) {
            return;
          }
          TRACE_ASYNC_BEGIN("benchmark", "transaction", i / transaction_size_);
          RunSingle(i, std::move(key));
        }));
  }));
}

void UpdateEntryBenchmark::CommitAndShutDown() {
  TRACE_ASYNC_BEGIN("benchmark", "commit", entry_count_ / transaction_size_);
  page_->Commit([this](ledger::Status status) {
    if (benchmark::QuitOnError(QuitLoopClosure(), status, "Page::Commit")) {
      return;
    }
    TRACE_ASYNC_END("benchmark", "commit", entry_count_ / transaction_size_);
    TRACE_ASYNC_END("benchmark", "transaction",
                    entry_count_ / transaction_size_);
    ShutDown();
  });
}

void UpdateEntryBenchmark::ShutDown() {
  // Shut down the Ledger process first as it relies on |tmp_dir_| storage.
  test::KillLedgerProcess(&component_controller_);
  loop_->Quit();
}

fit::closure UpdateEntryBenchmark::QuitLoopClosure() {
  return [this] { loop_->Quit(); };
}

}  // namespace benchmark
}  // namespace test

int main(int argc, const char** argv) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  std::string entry_count_str;
  size_t entry_count;
  std::string value_size_str;
  int value_size;
  std::string transaction_size_str;
  int transaction_size;
  if (!command_line.GetOptionValue(kEntryCountFlag.ToString(),
                                   &entry_count_str) ||
      !fxl::StringToNumberWithError(entry_count_str, &entry_count) ||
      entry_count <= 0 ||
      !command_line.GetOptionValue(kValueSizeFlag.ToString(),
                                   &value_size_str) ||
      !fxl::StringToNumberWithError(value_size_str, &value_size) ||
      value_size <= 0 ||
      !command_line.GetOptionValue(kTransactionSizeFlag.ToString(),
                                   &transaction_size_str) ||
      !fxl::StringToNumberWithError(transaction_size_str, &transaction_size) ||
      transaction_size < 0) {
    PrintUsage(argv[0]);
    return -1;
  }

  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  test::benchmark::UpdateEntryBenchmark app(&loop, entry_count, value_size,
                                            transaction_size);
  return test::benchmark::RunWithTracing(&loop, [&app] { app.Run(); });
}
