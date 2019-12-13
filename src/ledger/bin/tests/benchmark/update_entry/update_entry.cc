// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/trace/event.h>
#include <lib/zx/time.h>

#include <iostream>
#include <memory>

#include "peridot/lib/rng/test_random.h"
#include "src/ledger/bin/app/flags.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/platform/platform.h"
#include "src/ledger/bin/platform/scoped_tmp_dir.h"
#include "src/ledger/bin/testing/data_generator.h"
#include "src/ledger/bin/testing/get_ledger.h"
#include "src/ledger/bin/testing/get_page_ensure_initialized.h"
#include "src/ledger/bin/testing/quit_on_error.h"
#include "src/ledger/bin/testing/run_with_tracing.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/ledger/lib/vmo/strings.h"
#include "third_party/abseil-cpp/absl/flags/flag.h"
#include "third_party/abseil-cpp/absl/flags/parse.h"
#include "third_party/abseil-cpp/absl/strings/numbers.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

ABSL_FLAG(ssize_t, entry_count, -1, "number of entries to delete");
ABSL_FLAG(ssize_t, value_size, -1, "size of the values of entries");
ABSL_FLAG(ssize_t, transaction_size, -1, "number of element in the transaction");

namespace ledger {
namespace {
constexpr absl::string_view kStoragePath = "/data/benchmark/ledger/update_entry";

const int kKeySize = 100;

// Benchmark that measures a performance of Put() operation under the condition
// that it modifies the same entry.
//
// Parameters:
//   --entry-count=<int> the number of entries to be put
//   --value-size=<int> the size of the value for each entry
//   --transaction-size=<int> the size of a single transaction in number of put
//     operations. If equal to 0, every put operation will be executed
//     individually (implicit transaction).
class UpdateEntryBenchmark {
 public:
  UpdateEntryBenchmark(async::Loop* loop, std::unique_ptr<sys::ComponentContext> component_context,
                       int entry_count, int value_size, int transaction_size);
  UpdateEntryBenchmark(const UpdateEntryBenchmark&) = delete;
  UpdateEntryBenchmark& operator=(const UpdateEntryBenchmark&) = delete;

  void Run();

 private:
  void RunSingle(int i, std::vector<uint8_t> key);
  void CommitAndRunNext(int i, std::vector<uint8_t> key);

  void ShutDown();
  fit::closure QuitLoopClosure();

  async::Loop* const loop_;
  rng::TestRandom random_;
  DataGenerator generator_;

  std::unique_ptr<sys::ComponentContext> component_context_;
  std::unique_ptr<Platform> platform_;
  std::unique_ptr<ScopedTmpDir> tmp_dir_;
  const int entry_count_;
  const int transaction_size_;
  const int key_size_;
  const int value_size_;

  fuchsia::sys::ComponentControllerPtr component_controller_;
  LedgerPtr ledger_;
  PagePtr page_;
};

UpdateEntryBenchmark::UpdateEntryBenchmark(async::Loop* loop,
                                           std::unique_ptr<sys::ComponentContext> component_context,
                                           int entry_count, int value_size, int transaction_size)
    : loop_(loop),
      random_(0),
      generator_(&random_),
      component_context_(std::move(component_context)),
      platform_(MakePlatform()),
      tmp_dir_(platform_->file_system()->CreateScopedTmpDir(
          DetachedPath(convert::ToString(kStoragePath)))),
      entry_count_(entry_count),
      transaction_size_(transaction_size),
      key_size_(kKeySize),
      value_size_(value_size) {
  LEDGER_DCHECK(loop_);
  LEDGER_DCHECK(entry_count_ > 0);
  LEDGER_DCHECK(value_size_ > 0);
  LEDGER_DCHECK(key_size_ > 0);
  LEDGER_DCHECK(transaction_size_ >= 0);
}

void UpdateEntryBenchmark::Run() {
  LEDGER_LOG(INFO) << "--entry-count=" << entry_count_
                   << " --transaction-size=" << transaction_size_;
  Status status = GetLedger(component_context_.get(), component_controller_.NewRequest(), nullptr,
                            "", "update_entry", tmp_dir_->path(), QuitLoopClosure(), &ledger_,
                            kDefaultGarbageCollectionPolicy);
  if (QuitOnError(QuitLoopClosure(), status, "GetLedger")) {
    return;
  }
  GetPageEnsureInitialized(
      &ledger_, nullptr, DelayCallback::YES, QuitLoopClosure(),
      [this](Status status, PagePtr page, PageId id) {
        if (QuitOnError(QuitLoopClosure(), status, "GetPageEnsureInitialized")) {
          return;
        }
        page_ = std::move(page);
        std::vector<uint8_t> key = generator_.MakeKey(0, key_size_);
        if (transaction_size_ > 0) {
          page_->StartTransaction();
          page_->Sync([this, key = std::move(key)]() mutable {
            TRACE_ASYNC_BEGIN("benchmark", "transaction", 0);
            RunSingle(0, std::move(key));
          });
        } else {
          RunSingle(0, std::move(key));
        }
      });
}

void UpdateEntryBenchmark::RunSingle(int i, std::vector<uint8_t> key) {
  if (i == entry_count_) {
    ShutDown();
    return;
  }

  std::vector<uint8_t> value = generator_.MakeValue(value_size_);
  TRACE_ASYNC_BEGIN("benchmark", "put", i);
  page_->Put(key, std::move(value));
  page_->Sync([this, i, key = std::move(key)]() mutable {
    TRACE_ASYNC_END("benchmark", "put", i);
    if (transaction_size_ > 0 &&
        (i % transaction_size_ == transaction_size_ - 1 || i + 1 == entry_count_)) {
      CommitAndRunNext(i, std::move(key));
    } else {
      RunSingle(i + 1, std::move(key));
    }
  });
}

void UpdateEntryBenchmark::CommitAndRunNext(int i, std::vector<uint8_t> key) {
  TRACE_ASYNC_BEGIN("benchmark", "commit", i / transaction_size_);
  page_->Commit();
  page_->Sync([this, i, key = std::move(key)]() mutable {
    TRACE_ASYNC_END("benchmark", "commit", i / transaction_size_);
    TRACE_ASYNC_END("benchmark", "transaction", i / transaction_size_);

    if (i == entry_count_ - 1) {
      RunSingle(i + 1, std::move(key));
      return;
    }
    page_->StartTransaction();
    page_->Sync([this, i = i + 1, key = std::move(key)]() mutable {
      TRACE_ASYNC_BEGIN("benchmark", "transaction", i / transaction_size_);
      RunSingle(i, std::move(key));
    });
  });
}

void UpdateEntryBenchmark::ShutDown() {
  // Shut down the Ledger process first as it relies on |tmp_dir_| storage.
  KillLedgerProcess(&component_controller_);
  loop_->Quit();
}

fit::closure UpdateEntryBenchmark::QuitLoopClosure() {
  return [this] { loop_->Quit(); };
}

int Main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto component_context = sys::ComponentContext::Create();

  ssize_t entry_count = absl::GetFlag(FLAGS_entry_count);
  ssize_t value_size = absl::GetFlag(FLAGS_value_size);
  ssize_t transaction_size = absl::GetFlag(FLAGS_transaction_size);
  if (entry_count <= 0 || value_size <= 0 || transaction_size < 0) {
    std::cerr << "Incorrect parameter values" << std::endl;
    return 1;
  }

  UpdateEntryBenchmark app(&loop, std::move(component_context), entry_count, value_size,
                           transaction_size);
  return RunWithTracing(&loop, [&app] { app.Run(); });
}

}  // namespace
}  // namespace ledger

int main(int argc, char** argv) { return ledger::Main(argc, argv); }
