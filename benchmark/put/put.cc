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
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_number_conversions.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

constexpr ftl::StringView kStoragePath = "/data/benchmark/ledger/put";
constexpr ftl::StringView kEntryCountFlag = "entry-count";
constexpr ftl::StringView kTransactionSizeFlag = "transaction-size";
constexpr ftl::StringView kKeySizeFlag = "key-size";
constexpr ftl::StringView kValueSizeFlag = "value-size";
constexpr ftl::StringView kUpdateFlag = "update";

void PrintUsage(const char* executable_name) {
  std::cout << "Usage: " << executable_name << " --" << kEntryCountFlag
            << "=<int> --" << kTransactionSizeFlag << "=<int> --"
            << kKeySizeFlag << "=<int> --" << kValueSizeFlag << "=<int> ["
            << kUpdateFlag << "]" << std::endl;
}

bool GetPositiveIntValue(const ftl::CommandLine& command_line,
                         ftl::StringView flag,
                         int* value) {
  std::string value_str;
  int found_value;
  if (!command_line.GetOptionValue(flag.ToString(), &value_str) ||
      !ftl::StringToNumberWithError(value_str, &found_value) ||
      found_value <= 0) {
    return false;
  }
  *value = found_value;
  return true;
}

}  // namespace

namespace benchmark {

PutBenchmark::PutBenchmark(int entry_count,
                           int transaction_size,
                           int key_size,
                           int value_size,
                           bool update)
    : tmp_dir_(kStoragePath),
      application_context_(app::ApplicationContext::CreateFromStartupInfo()),
      entry_count_(entry_count),
      transaction_size_(transaction_size),
      key_size_(key_size),
      value_size_(value_size),
      update_(update) {
  FTL_DCHECK(entry_count > 0);
  FTL_DCHECK(transaction_size > 0);
  FTL_DCHECK(key_size > 0);
  FTL_DCHECK(value_size > 0);
  tracing::InitializeTracer(application_context_.get(),
                            {"benchmark_ledger_put"});
}

void PutBenchmark::Run() {
  ledger::LedgerPtr ledger =
      benchmark::GetLedger(application_context_.get(), &ledger_controller_,
                           "put", tmp_dir_.path(), false, "");
  InitializeKeys(ftl::MakeCopyable([ this, ledger = std::move(ledger) ](
      std::vector<fidl::Array<uint8_t>> keys) {
    benchmark::GetPageEnsureInitialized(
        ledger.get(), nullptr,
        ftl::MakeCopyable([ this, keys = std::move(keys) ](ledger::PagePtr page,
                                                           auto id) mutable {
          page_ = std::move(page);
          if (transaction_size_ > 1) {
            page_->StartTransaction(ftl::MakeCopyable([
              this, keys = std::move(keys)
            ](ledger::Status status) mutable {
              if (benchmark::QuitOnError(status, "Page::StartTransaction")) {
                return;
              }
              TRACE_ASYNC_BEGIN("benchmark", "transaction", 0);
              RunSingle(0, std::move(keys));
            }));
          } else {
            RunSingle(0, std::move(keys));
          }
        }));
  }));
}

void PutBenchmark::InitializeKeys(
    std::function<void(std::vector<fidl::Array<uint8_t>>)> on_done) {
  std::vector<fidl::Array<uint8_t>> keys;
  for (int i = 0; i < entry_count_; ++i) {
    keys.push_back(benchmark::MakeKey(i, key_size_));
  }
  if (!update_) {
    on_done(std::move(keys));
    return;
  }
  AddInitialEntries(0, std::move(keys), std::move(on_done));
}

void PutBenchmark::AddInitialEntries(
    int i,
    std::vector<fidl::Array<uint8_t>> keys,
    std::function<void(std::vector<fidl::Array<uint8_t>>)> on_done) {
  if (i == entry_count_) {
    on_done(std::move(keys));
    return;
  }
  fidl::Array<uint8_t> value = benchmark::MakeValue(value_size_);
  page_->Put(keys[i].Clone(), std::move(value), ftl::MakeCopyable([
               this, i, keys = std::move(keys), on_done = std::move(on_done)
             ](ledger::Status status) mutable {
               if (benchmark::QuitOnError(status, "Page::Put")) {
                 return;
               }
               AddInitialEntries(i + 1, std::move(keys), std::move(on_done));
             }));
}

void PutBenchmark::RunSingle(int i, std::vector<fidl::Array<uint8_t>> keys) {
  if (i == entry_count_) {
    if (transaction_size_ > 1) {
      CommitAndShutDown();
    } else {
      ShutDown();
    }
    return;
  }

  fidl::Array<uint8_t> value = benchmark::MakeValue(value_size_);
  TRACE_ASYNC_BEGIN("benchmark", "put", i);
  page_->Put(std::move(keys[i]), std::move(value),
             ftl::MakeCopyable([ this, i, keys = std::move(keys) ](
                 ledger::Status status) mutable {
               if (benchmark::QuitOnError(status, "Page::Put")) {
                 return;
               }
               TRACE_ASYNC_END("benchmark", "put", i);
               if (transaction_size_ > 1 &&
                   i % transaction_size_ == transaction_size_ - 1) {
                 CommitAndRunNext(i, std::move(keys));
               } else {
                 RunSingle(i + 1, std::move(keys));
               }
             }));
}

void PutBenchmark::CommitAndRunNext(int i,
                                    std::vector<fidl::Array<uint8_t>> keys) {
  TRACE_ASYNC_BEGIN("benchmark", "commit", i / transaction_size_);
  page_->Commit(ftl::MakeCopyable(
      [ this, i, keys = std::move(keys) ](ledger::Status status) mutable {
        if (benchmark::QuitOnError(status, "Page::Commit")) {
          return;
        }
        TRACE_ASYNC_END("benchmark", "commit", i / transaction_size_);
        TRACE_ASYNC_END("benchmark", "transaction", i / transaction_size_);

        page_->StartTransaction(ftl::MakeCopyable([
          this, i = i + 1, keys = std::move(keys)
        ](ledger::Status status) mutable {
          if (benchmark::QuitOnError(status, "Page::StartTransaction")) {
            return;
          }
          TRACE_ASYNC_BEGIN("benchmark", "transaction", i / transaction_size_);
          RunSingle(i, std::move(keys));
        }));
      }));
}

void PutBenchmark::CommitAndShutDown() {
  TRACE_ASYNC_BEGIN("benchmark", "commit", entry_count_ / transaction_size_);
  page_->Commit([this](ledger::Status status) {
    if (benchmark::QuitOnError(status, "Page::Commit")) {
      return;
    }
    TRACE_ASYNC_END("benchmark", "commit", entry_count_ / transaction_size_);
    TRACE_ASYNC_END("benchmark", "transaction",
                    entry_count_ / transaction_size_);
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

  int entry_count;
  int transaction_size;
  int key_size;
  int value_size;
  bool update = command_line.HasOption(kUpdateFlag.ToString());
  if (!GetPositiveIntValue(command_line, kEntryCountFlag, &entry_count) ||
      !GetPositiveIntValue(command_line, kTransactionSizeFlag,
                           &transaction_size) ||
      !GetPositiveIntValue(command_line, kKeySizeFlag, &key_size) ||
      !GetPositiveIntValue(command_line, kValueSizeFlag, &value_size)) {
    PrintUsage(argv[0]);
    return -1;
  }

  mtl::MessageLoop loop;
  benchmark::PutBenchmark app(entry_count, transaction_size, key_size,
                              value_size, update);
  loop.task_runner()->PostTask([&app] { app.Run(); });
  loop.Run();
  return 0;
}
