// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/tests/benchmark/put/put.h"

#include <trace/event.h>

#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "peridot/bin/ledger/testing/get_ledger.h"
#include "peridot/bin/ledger/testing/quit_on_error.h"
#include "peridot/lib/convert/convert.h"

namespace {

constexpr fxl::StringView kStoragePath = "/data/benchmark/ledger/put";

}  // namespace

namespace test {
namespace benchmark {

PutBenchmark::PutBenchmark(
    int entry_count,
    int transaction_size,
    int key_size,
    int value_size,
    bool update,
    PageDataGenerator::ReferenceStrategy reference_strategy,
    uint64_t seed)
    : generator_(seed),
      tmp_dir_(kStoragePath),
      application_context_(app::ApplicationContext::CreateFromStartupInfo()),
      entry_count_(entry_count),
      transaction_size_(transaction_size),
      key_size_(key_size),
      value_size_(value_size),
      update_(update),
      page_watcher_binding_(this),
      reference_strategy_(reference_strategy) {
  FXL_DCHECK(entry_count > 0);
  FXL_DCHECK(transaction_size >= 0);
  FXL_DCHECK(key_size > 0);
  FXL_DCHECK(value_size > 0);
}

void PutBenchmark::Run() {
  FXL_LOG(INFO) << "--entry-count=" << entry_count_
                << " --transaction-size=" << transaction_size_
                << " --key-size=" << key_size_
                << " --value-size=" << value_size_ << " --refs="
                << (reference_strategy_ ==
                            PageDataGenerator::ReferenceStrategy::INLINE
                        ? "off"
                        : "on")
                << (update_ ? " --update" : "");
  ledger::LedgerPtr ledger;
  ledger::Status status = test::GetLedger(
      fsl::MessageLoop::GetCurrent(), application_context_.get(),
      &application_controller_, nullptr, "put", tmp_dir_.path(), &ledger);
  QuitOnError(status, "GetLedger");

  fidl::Array<uint8_t> id;
  status = test::GetPageEnsureInitialized(fsl::MessageLoop::GetCurrent(),
                                          &ledger, nullptr, &page_, &id);
  QuitOnError(status, "GetPageEnsureInitialized");

  InitializeKeys(
      fxl::MakeCopyable([this, ledger = std::move(ledger)](
                            std::vector<fidl::Array<uint8_t>> keys) mutable {
        if (transaction_size_ > 0) {
          page_->StartTransaction(fxl::MakeCopyable(
              [this, keys = std::move(keys)](ledger::Status status) mutable {
                if (benchmark::QuitOnError(status, "Page::StartTransaction")) {
                  return;
                }
                TRACE_ASYNC_BEGIN("benchmark", "transaction", 0);
                BindWatcher(std::move(keys));
              }));
        } else {
          BindWatcher(std::move(keys));
        }
      }));
}

void PutBenchmark::OnChange(ledger::PageChangePtr page_change,
                            ledger::ResultState /*result_state*/,
                            const OnChangeCallback& callback) {
  for (auto const& change : page_change->changes) {
    size_t key_number = std::stoul(convert::ToString(change->key));
    if (keys_to_receive_.find(key_number) != keys_to_receive_.end()) {
      TRACE_ASYNC_END("benchmark", "local_change_notification", key_number);
      keys_to_receive_.erase(key_number);
    }
  }
  if (keys_to_receive_.empty()) {
    ShutDown();
  }
  callback(nullptr);
}

void PutBenchmark::InitializeKeys(
    std::function<void(std::vector<fidl::Array<uint8_t>>)> on_done) {
  std::vector<fidl::Array<uint8_t>> keys =
      generator_.MakeKeys(entry_count_, key_size_, entry_count_);
  std::vector<fidl::Array<uint8_t>> keys_cloned;
  for (int i = 0; i < entry_count_; ++i) {
    keys_cloned.push_back(keys[i].Clone());
    if (transaction_size_ == 0 ||
        i % transaction_size_ == transaction_size_ - 1) {
      keys_to_receive_.insert(std::stoul(convert::ToString(keys[i])));
    }
  }
  // Last key should always be recorded so the last transaction is not lost.
  size_t last_key_number = std::stoul(convert::ToString(keys.back()));
  keys_to_receive_.insert(last_key_number);
  if (!update_) {
    on_done(std::move(keys));
    return;
  }
  page_data_generator_.Populate(
      &page_, std::move(keys_cloned), value_size_, keys_cloned.size(),
      reference_strategy_, ledger::Priority::EAGER,
      fxl::MakeCopyable([keys = std::move(keys), on_done = std::move(on_done)](
                            ledger::Status status) mutable {
        if (QuitOnError(status, "PageDataGenerator::Populate")) {
          return;
        }
        on_done(std::move(keys));
      }));
}

void PutBenchmark::BindWatcher(std::vector<fidl::Array<uint8_t>> keys) {
  ledger::PageSnapshotPtr snapshot;
  page_->GetSnapshot(
      snapshot.NewRequest(), nullptr, page_watcher_binding_.NewBinding(),
      fxl::MakeCopyable(
          [this, keys = std::move(keys)](ledger::Status status) mutable {
            if (benchmark::QuitOnError(status, "GetSnapshot")) {
              return;
            }
            RunSingle(0, std::move(keys));
          }));
}

void PutBenchmark::RunSingle(int i, std::vector<fidl::Array<uint8_t>> keys) {
  if (i == entry_count_) {
    // All sent, waiting for watcher notification before shutting down.
    return;
  }

  fidl::Array<uint8_t> value = generator_.MakeValue(value_size_);
  size_t key_number = std::stoul(convert::ToString(keys[i]));
  if (transaction_size_ == 0) {
    TRACE_ASYNC_BEGIN("benchmark", "local_change_notification", key_number);
  }
  PutEntry(std::move(keys[i]), std::move(value),
           fxl::MakeCopyable(
               [this, i, key_number, keys = std::move(keys)]() mutable {
                 if (transaction_size_ > 0 &&
                     (i % transaction_size_ == transaction_size_ - 1 ||
                      i + 1 == entry_count_)) {
                   CommitAndRunNext(i, key_number, std::move(keys));
                 } else {
                   RunSingle(i + 1, std::move(keys));
                 }
               }));
}

void PutBenchmark::PutEntry(fidl::Array<uint8_t> key,
                            fidl::Array<uint8_t> value,
                            std::function<void()> on_done) {
  auto trace_event_id = TRACE_NONCE();
  TRACE_ASYNC_BEGIN("benchmark", "put", trace_event_id);
  if (reference_strategy_ == PageDataGenerator::ReferenceStrategy::INLINE) {
    page_->Put(
        std::move(key), std::move(value),
        [trace_event_id, on_done = std::move(on_done)](ledger::Status status) {
          if (QuitOnError(status, "Page::Put")) {
            return;
          }
          TRACE_ASYNC_END("benchmark", "put", trace_event_id);
          on_done();
        });
    return;
  }
  fsl::SizedVmo vmo;
  FXL_CHECK(fsl::VmoFromString(convert::ToStringView(value), &vmo));
  TRACE_ASYNC_BEGIN("benchmark", "create reference", trace_event_id);
  page_->CreateReferenceFromVmo(
      std::move(vmo).ToTransport(),
      fxl::MakeCopyable(
          [this, trace_event_id, key = std::move(key),
           on_done = std::move(on_done)](
              ledger::Status status, ledger::ReferencePtr reference) mutable {
            if (QuitOnError(status, "Page::CreateReferenceFromVmo")) {
              return;
            }
            TRACE_ASYNC_END("benchmark", "create reference", trace_event_id);
            TRACE_ASYNC_BEGIN("benchmark", "put reference", trace_event_id);
            page_->PutReference(
                std::move(key), std::move(reference), ledger::Priority::EAGER,
                [trace_event_id,
                 on_done = std::move(on_done)](ledger::Status status) {
                  if (QuitOnError(status, "Page::PutReference")) {
                    return;
                  }
                  TRACE_ASYNC_END("benchmark", "put reference", trace_event_id);
                  TRACE_ASYNC_END("benchmark", "put", trace_event_id);
                  on_done();
                });
          }));
}

void PutBenchmark::CommitAndRunNext(int i,
                                    size_t key_number,
                                    std::vector<fidl::Array<uint8_t>> keys) {
  TRACE_ASYNC_BEGIN("benchmark", "local_change_notification", key_number);
  TRACE_ASYNC_BEGIN("benchmark", "commit", i / transaction_size_);
  page_->Commit(fxl::MakeCopyable([this, i, key_number, keys = std::move(keys)](
                                      ledger::Status status) mutable {
    if (benchmark::QuitOnError(status, "Page::Commit")) {
      return;
    }
    TRACE_ASYNC_END("benchmark", "commit", i / transaction_size_);
    TRACE_ASYNC_END("benchmark", "transaction", i / transaction_size_);

    if (i == entry_count_ - 1) {
      RunSingle(i + 1, std::move(keys));
      return;
    }
    page_->StartTransaction(
        fxl::MakeCopyable([this, i = i + 1, keys = std::move(keys)](
                              ledger::Status status) mutable {
          if (benchmark::QuitOnError(status, "Page::StartTransaction")) {
            return;
          }
          TRACE_ASYNC_BEGIN("benchmark", "transaction", i / transaction_size_);
          RunSingle(i, std::move(keys));
        }));
  }));
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
