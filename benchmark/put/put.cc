// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/benchmark/put/put.h"

#include "apps/ledger/benchmark/lib/convert.h"
#include "apps/ledger/benchmark/lib/data.h"
#include "apps/ledger/benchmark/lib/get_ledger.h"
#include "apps/ledger/benchmark/lib/logging.h"
#include "apps/tracing/lib/trace/event.h"
#include "apps/tracing/lib/trace/provider.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

constexpr ftl::StringView kStoragePath = "/data/benchmark/ledger/put";

}  // namespace

namespace benchmark {

PutBenchmark::PutBenchmark(int entry_count,
                           int transaction_size,
                           int key_size,
                           int value_size,
                           bool update)
    : application_context_(app::ApplicationContext::CreateFromStartupInfo()),
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
  application_controller_ = std::make_unique<app::ApplicationControllerPtr>();
  ledger_controller_ = std::make_unique<ledger::LedgerControllerPtr>();
  tmp_dir_ = std::make_unique<files::ScopedTempDir>(kStoragePath);

  ledger::LedgerPtr ledger = benchmark::GetLedger(
      application_context_.get(), application_controller_.get(), "put",
      tmp_dir_->path(), false, "", ledger_controller_.get());
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

void PutBenchmark::ResetLedger() {
  page_.reset();
  (*ledger_controller_)->Terminate();
  bool response = application_controller_->WaitForIncomingResponseWithTimeout(
      ftl::TimeDelta::FromSeconds(1));
  FTL_DCHECK(!response);
  FTL_DCHECK(application_controller_->encountered_error());
  if (on_done_) {
    on_done_();
  }
}

void PutBenchmark::ShutDown() {
  mtl::MessageLoop::GetCurrent()->PostQuitTask();
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
      CommitAndReset();
    } else {
      ResetLedger();
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

void PutBenchmark::CommitAndReset() {
  TRACE_ASYNC_BEGIN("benchmark", "commit", entry_count_ / transaction_size_);
  page_->Commit([this](ledger::Status status) {
    if (benchmark::QuitOnError(status, "Page::Commit")) {
      return;
    }
    TRACE_ASYNC_END("benchmark", "commit", entry_count_ / transaction_size_);
    TRACE_ASYNC_END("benchmark", "transaction",
                    entry_count_ / transaction_size_);
    ResetLedger();
  });
}

}  // namespace benchmark
