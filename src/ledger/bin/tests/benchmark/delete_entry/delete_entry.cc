// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/time.h>

#include <iostream>
#include <memory>

#include <trace/event.h>

#include "peridot/lib/rng/test_random.h"
#include "src/ledger/bin/app/flags.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/filesystem/get_directory_content_size.h"
#include "src/ledger/bin/testing/data_generator.h"
#include "src/ledger/bin/testing/get_ledger.h"
#include "src/ledger/bin/testing/get_page_ensure_initialized.h"
#include "src/ledger/bin/testing/page_data_generator.h"
#include "src/ledger/bin/testing/quit_on_error.h"
#include "src/ledger/bin/testing/run_with_tracing.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/callback/waiter.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_number_conversions.h"

namespace ledger {
namespace {

constexpr fxl::StringView kBinaryPath =
    "fuchsia-pkg://fuchsia.com/ledger_benchmarks#meta/delete_entry.cmx";
constexpr fxl::StringView kStoragePath = "/data/benchmark/ledger/delete_entry";
constexpr fxl::StringView kEntryCountFlag = "entry-count";
constexpr fxl::StringView kTransactionSizeFlag = "transaction-size";
constexpr fxl::StringView kKeySizeFlag = "key-size";
constexpr fxl::StringView kValueSizeFlag = "value-size";

void PrintUsage() {
  std::cout << "Usage: trace record "
            << kBinaryPath
            // Comment to make clang format not break formatting.
            << " --" << kEntryCountFlag << "=<int>"
            << " --" << kTransactionSizeFlag << "=<int>"
            << " --" << kKeySizeFlag << "=<int>"
            << " --" << kValueSizeFlag << "=<int>" << std::endl;
}

// Benchmark that measures the time taken to delete an entry from a page.
//
// Parameters:
//   --entry-count=<int> the number of entries to be put and deleted
//   --transaction_size=<int> number of delete operations in each transaction. 0
//     means no explicit transactions.
//   --key-size=<int> size of the keys for the entries
//   --value-size=<int> the size of a single value in bytes
class DeleteEntryBenchmark {
 public:
  DeleteEntryBenchmark(async::Loop* loop, std::unique_ptr<sys::ComponentContext> component_context,
                       size_t entry_count, size_t transaction_size, size_t key_size,
                       size_t value_size);

  void Run();

 private:
  void Populate();
  void RunSingle(size_t i);
  void CommitAndRunNext(size_t i);
  void ShutDown();
  fit::closure QuitLoopClosure();

  async::Loop* const loop_;
  rng::TestRandom random_;
  files::ScopedTempDir tmp_dir_;
  DataGenerator generator_;
  PageDataGenerator page_data_generator_;
  std::unique_ptr<sys::ComponentContext> component_context_;
  const size_t entry_count_;
  const size_t transaction_size_;
  const size_t key_size_;
  const size_t value_size_;
  fuchsia::sys::ComponentControllerPtr component_controller_;
  LedgerPtr ledger_;
  PagePtr page_;
  std::vector<std::vector<uint8_t>> keys_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DeleteEntryBenchmark);
};

DeleteEntryBenchmark::DeleteEntryBenchmark(async::Loop* loop,
                                           std::unique_ptr<sys::ComponentContext> component_context,
                                           size_t entry_count, size_t transaction_size,
                                           size_t key_size, size_t value_size)
    : loop_(loop),
      random_(0),
      tmp_dir_(kStoragePath),
      generator_(&random_),
      page_data_generator_(&random_),
      component_context_(std::move(component_context)),
      entry_count_(entry_count),
      transaction_size_(transaction_size),
      key_size_(key_size),
      value_size_(value_size) {
  FXL_DCHECK(loop_);
  FXL_DCHECK(entry_count_ > 0);
  FXL_DCHECK(transaction_size_ >= 0);
  FXL_DCHECK(key_size_ > 0);
  FXL_DCHECK(value_size_ > 0);
}

void DeleteEntryBenchmark::Run() {
  Status status = GetLedger(component_context_.get(), component_controller_.NewRequest(), nullptr,
                            "", "delete_entry", DetachedPath(tmp_dir_.path()), QuitLoopClosure(),
                            &ledger_, kDefaultGarbageCollectionPolicy);
  if (QuitOnError(QuitLoopClosure(), status, "GetLedger")) {
    return;
  }

  GetPageEnsureInitialized(&ledger_, nullptr, DelayCallback::YES, QuitLoopClosure(),
                           [this](Status status, PagePtr page, PageId id) {
                             if (QuitOnError(QuitLoopClosure(), status, "Page initialization")) {
                               return;
                             }
                             page_ = std::move(page);
                             Populate();
                           });
}

void DeleteEntryBenchmark::Populate() {
  auto keys = generator_.MakeKeys(entry_count_, key_size_, entry_count_);
  for (size_t i = 0; i < entry_count_; i++) {
    keys_.push_back(keys[i]);
  }

  page_data_generator_.Populate(
      &page_, std::move(keys), value_size_, entry_count_,
      PageDataGenerator::ReferenceStrategy::REFERENCE, Priority::EAGER, [this](Status status) {
        if (status != Status::OK) {
          QuitOnError(QuitLoopClosure(), status, "PageGenerator::Populate");
          return;
        }
        if (transaction_size_ > 0) {
          page_->StartTransaction();
          page_->Sync([this] {
            TRACE_ASYNC_BEGIN("benchmark", "transaction", 0);
            RunSingle(0);
          });
        } else {
          RunSingle(0);
        }
      });
}

void DeleteEntryBenchmark::RunSingle(size_t i) {
  if (i == entry_count_) {
    ShutDown();

    uint64_t tmp_dir_size = 0;
    FXL_CHECK(GetDirectoryContentSize(DetachedPath(tmp_dir_.path()), &tmp_dir_size));
    TRACE_COUNTER("benchmark", "ledger_directory_size", 0, "directory_size",
                  TA_UINT64(tmp_dir_size));
    return;
  }

  TRACE_ASYNC_BEGIN("benchmark", "delete_entry", i);
  page_->Delete(std::move(keys_[i]));
  page_->Sync([this, i]() {
    TRACE_ASYNC_END("benchmark", "delete_entry", i);
    if (transaction_size_ > 0 &&
        (i % transaction_size_ == transaction_size_ - 1 || i + 1 == entry_count_)) {
      CommitAndRunNext(i);
    } else {
      RunSingle(i + 1);
    }
  });
}

void DeleteEntryBenchmark::CommitAndRunNext(size_t i) {
  TRACE_ASYNC_BEGIN("benchmark", "commit", i / transaction_size_);
  page_->Commit();
  page_->Sync([this, i]() {
    TRACE_ASYNC_END("benchmark", "commit", i / transaction_size_);
    TRACE_ASYNC_END("benchmark", "transaction", i / transaction_size_);

    if (i == entry_count_ - 1) {
      RunSingle(i + 1);
      return;
    }
    page_->StartTransaction();
    page_->Sync([this, i = i + 1]() {
      TRACE_ASYNC_BEGIN("benchmark", "transaction", i / transaction_size_);
      RunSingle(i);
    });
  });
}

void DeleteEntryBenchmark::ShutDown() {
  KillLedgerProcess(&component_controller_);
  loop_->Quit();
}

fit::closure DeleteEntryBenchmark::QuitLoopClosure() {
  return [this] { loop_->Quit(); };
}

int Main(int argc, const char** argv) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto component_context = sys::ComponentContext::Create();

  std::string entry_count_str;
  size_t entry_count;
  std::string transaction_size_str;
  size_t transaction_size;
  std::string key_size_str;
  size_t key_size;
  std::string value_size_str;
  size_t value_size;
  if (!command_line.GetOptionValue(kEntryCountFlag.ToString(), &entry_count_str) ||
      !fxl::StringToNumberWithError(entry_count_str, &entry_count) || entry_count == 0 ||
      !command_line.GetOptionValue(kTransactionSizeFlag.ToString(), &transaction_size_str) ||
      !fxl::StringToNumberWithError(transaction_size_str, &transaction_size) ||
      !command_line.GetOptionValue(kKeySizeFlag.ToString(), &key_size_str) ||
      !fxl::StringToNumberWithError(key_size_str, &key_size) || key_size == 0 ||
      !command_line.GetOptionValue(kValueSizeFlag.ToString(), &value_size_str) ||
      !fxl::StringToNumberWithError(value_size_str, &value_size) || value_size == 0) {
    PrintUsage();
    return -1;
  }

  DeleteEntryBenchmark app(&loop, std::move(component_context), entry_count, transaction_size,
                           key_size, value_size);

  return RunWithTracing(&loop, [&app] { app.Run(); });
}

}  // namespace
}  // namespace ledger

int main(int argc, const char** argv) { return ledger::Main(argc, argv); }
