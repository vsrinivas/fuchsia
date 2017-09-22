// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/test/benchmark/put/put.h"

#include <trace/event.h>

#include "peridot/bin/ledger/convert/convert.h"
#include "peridot/bin/ledger/test/benchmark/lib/logging.h"
#include "peridot/bin/ledger/test/get_ledger.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"

namespace {

constexpr fxl::StringView kStoragePath = "/data/benchmark/ledger/put";

constexpr size_t kMaxInlineDataSize = ZX_CHANNEL_MAX_MSG_BYTES * 9 / 10;

}  // namespace

namespace test {
namespace benchmark {

PutBenchmark::PutBenchmark(int entry_count,
                           int transaction_size,
                           int key_size,
                           int value_size,
                           bool update,
                           ReferenceStrategy reference_strategy,
                           uint64_t seed)
    : generator_(seed),
      tmp_dir_(kStoragePath),
      application_context_(app::ApplicationContext::CreateFromStartupInfo()),
      token_provider_impl_("",
                           "sync_user",
                           "sync_user@google.com",
                           "client_id"),
      entry_count_(entry_count),
      transaction_size_(transaction_size),
      key_size_(key_size),
      value_size_(value_size),
      update_(update) {
  FXL_DCHECK(entry_count > 0);
  FXL_DCHECK(transaction_size > 0);
  FXL_DCHECK(key_size > 0);
  FXL_DCHECK(value_size > 0);
  switch (reference_strategy) {
    case ReferenceStrategy::ON:
      should_put_as_reference_ = [](size_t value_size) { return true; };
      break;
    case ReferenceStrategy::OFF:
      should_put_as_reference_ = [](size_t value_size) { return false; };
      break;
    case ReferenceStrategy::AUTO:
      should_put_as_reference_ = [](size_t value_size) {
        return value_size > kMaxInlineDataSize;
      };
      break;
  }
}

void PutBenchmark::Run() {
  FXL_LOG(INFO) << "--entry-count=" << entry_count_
                << " --transaction-size=" << transaction_size_
                << " --key-size=" << key_size_
                << " --value-size=" << value_size_ << (update_ ? "update" : "");
  ledger::LedgerPtr ledger;
  ledger::Status status = test::GetLedger(
      fsl::MessageLoop::GetCurrent(), application_context_.get(),
      &application_controller_, &token_provider_impl_, "put", tmp_dir_.path(),
      test::SyncState::DISABLED, "", &ledger);
  QuitOnError(status, "GetLedger");

  InitializeKeys(fxl::MakeCopyable([ this, ledger = std::move(ledger) ](
      std::vector<fidl::Array<uint8_t>> keys) mutable {
    fidl::Array<uint8_t> id;
    ledger::Status status = test::GetPageEnsureInitialized(
        fsl::MessageLoop::GetCurrent(), &ledger, nullptr, &page_, &id);
    QuitOnError(status, "GetPageEnsureInitialized");
    if (transaction_size_ > 1) {
      page_->StartTransaction(fxl::MakeCopyable(
          [ this, keys = std::move(keys) ](ledger::Status status) mutable {
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
}

void PutBenchmark::InitializeKeys(
    std::function<void(std::vector<fidl::Array<uint8_t>>)> on_done) {
  std::vector<fidl::Array<uint8_t>> keys;
  keys.reserve(entry_count_);
  for (int i = 0; i < entry_count_; ++i) {
    keys.push_back(generator_.MakeKey(i, key_size_));
  }
  if (!update_) {
    on_done(std::move(keys));
    return;
  }
  AddInitialEntries(0, std::move(keys), std::move(on_done));
}

void PutBenchmark::PutEntry(fidl::Array<uint8_t> key,
                            fidl::Array<uint8_t> value,
                            std::function<void(ledger::Status)> put_callback) {
  if (!should_put_as_reference_(value.size())) {
    page_->Put(std::move(key), std::move(value), put_callback);
    return;
  }
  zx::vmo vmo;
  FXL_CHECK(fsl::VmoFromString(convert::ToString(value), &vmo));
  page_->CreateReferenceFromVmo(
      std::move(vmo), fxl::MakeCopyable([
        this, key = std::move(key), put_callback = std::move(put_callback)
      ](ledger::Status status, ledger::ReferencePtr reference) mutable {
        if (benchmark::QuitOnError(status, "Page::CreateReferenceFromVmo")) {
          return;
        }
        page_->PutReference(std::move(key), std::move(reference),
                            ledger::Priority::EAGER, put_callback);
      }));
}

void PutBenchmark::AddInitialEntries(
    int i,
    std::vector<fidl::Array<uint8_t>> keys,
    std::function<void(std::vector<fidl::Array<uint8_t>>)> on_done) {
  if (i == entry_count_) {
    on_done(std::move(keys));
    return;
  }
  fidl::Array<uint8_t> value = generator_.MakeValue(value_size_);
  PutEntry(keys[i].Clone(), std::move(value), fxl::MakeCopyable([
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

  fidl::Array<uint8_t> value = generator_.MakeValue(value_size_);
  TRACE_ASYNC_BEGIN("benchmark", "put", i);
  PutEntry(std::move(keys[i]), std::move(value),
           fxl::MakeCopyable([ this, i, keys = std::move(keys) ](
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
  page_->Commit(fxl::MakeCopyable(
      [ this, i, keys = std::move(keys) ](ledger::Status status) mutable {
        if (benchmark::QuitOnError(status, "Page::Commit")) {
          return;
        }
        TRACE_ASYNC_END("benchmark", "commit", i / transaction_size_);
        TRACE_ASYNC_END("benchmark", "transaction", i / transaction_size_);

        page_->StartTransaction(fxl::MakeCopyable([
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
  application_controller_->Kill();
  application_controller_.WaitForIncomingResponseWithTimeout(
      fxl::TimeDelta::FromSeconds(5));
  fsl::MessageLoop::GetCurrent()->PostQuitTask();
}

}  // namespace benchmark
}  // namespace test
