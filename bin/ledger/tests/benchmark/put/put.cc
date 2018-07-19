// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/tests/benchmark/put/put.h"

#include <lib/fit/function.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/logging.h>
#include <lib/zx/time.h>
#include <trace/event.h>

#include "peridot/bin/ledger/testing/get_ledger.h"
#include "peridot/bin/ledger/testing/quit_on_error.h"
#include "peridot/lib/convert/convert.h"

namespace {

constexpr fxl::StringView kStoragePath = "/data/benchmark/ledger/put";

}  // namespace

namespace test {
namespace benchmark {

PutBenchmark::PutBenchmark(
    async::Loop* loop, int entry_count, int transaction_size, int key_size,
    int value_size, bool update,
    PageDataGenerator::ReferenceStrategy reference_strategy, uint64_t seed)
    : loop_(loop),
      generator_(seed),
      tmp_dir_(kStoragePath),
      startup_context_(component::StartupContext::CreateFromStartupInfo()),
      entry_count_(entry_count),
      transaction_size_(transaction_size),
      key_size_(key_size),
      value_size_(value_size),
      update_(update),
      page_watcher_binding_(this),
      reference_strategy_(reference_strategy) {
  FXL_DCHECK(loop_);
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
  test::GetLedger(
      startup_context_.get(), component_controller_.NewRequest(), nullptr,
      "put", ledger::DetachedPath(tmp_dir_.path()), QuitLoopClosure(),
      [this](ledger::Status status, ledger::LedgerPtr ledger) {
        if (QuitOnError(QuitLoopClosure(), status, "GetLedger")) {
          return;
        }

        ledger_ = std::move(ledger);

        test::GetPageEnsureInitialized(
            &ledger_, nullptr, QuitLoopClosure(),
            [this](ledger::Status status, ledger::PagePtr page,
                   ledger::PageId id) mutable {
              if (QuitOnError(QuitLoopClosure(), status,
                              "GetPageEnsureInitialized")) {
                return;
              }
              page_ = std::move(page);

              InitializeKeys(
                  [this](std::vector<fidl::VectorPtr<uint8_t>> keys) mutable {
                    if (transaction_size_ > 0) {
                      page_->StartTransaction(
                          [this, keys = std::move(keys)](
                              ledger::Status status) mutable {
                            if (QuitOnError(QuitLoopClosure(), status,
                                            "Page::StartTransaction")) {
                              return;
                            }
                            TRACE_ASYNC_BEGIN("benchmark", "transaction", 0);
                            BindWatcher(std::move(keys));
                          });
                    } else {
                      BindWatcher(std::move(keys));
                    }
                  });
            });
      });
}

void PutBenchmark::OnChange(ledger::PageChange page_change,
                            ledger::ResultState /*result_state*/,
                            OnChangeCallback callback) {
  for (auto const& change : *page_change.changed_entries) {
    size_t key_number = std::stoul(convert::ToString(change.key));
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
    fit::function<void(std::vector<fidl::VectorPtr<uint8_t>>)> on_done) {
  std::vector<fidl::VectorPtr<uint8_t>> keys =
      generator_.MakeKeys(entry_count_, key_size_, entry_count_);
  std::vector<fidl::VectorPtr<uint8_t>> keys_cloned;
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
      [this, keys = std::move(keys),
       on_done = std::move(on_done)](ledger::Status status) mutable {
        if (QuitOnError(QuitLoopClosure(), status,
                        "PageDataGenerator::Populate")) {
          return;
        }
        on_done(std::move(keys));
      });
}

void PutBenchmark::BindWatcher(std::vector<fidl::VectorPtr<uint8_t>> keys) {
  ledger::PageSnapshotPtr snapshot;
  page_->GetSnapshot(
      snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
      page_watcher_binding_.NewBinding(),
      [this, keys = std::move(keys)](ledger::Status status) mutable {
        if (QuitOnError(QuitLoopClosure(), status, "GetSnapshot")) {
          return;
        }
        RunSingle(0, std::move(keys));
      });
}

void PutBenchmark::RunSingle(int i,
                             std::vector<fidl::VectorPtr<uint8_t>> keys) {
  if (i == entry_count_) {
    // All sent, waiting for watcher notification before shutting down.
    return;
  }

  fidl::VectorPtr<uint8_t> value = generator_.MakeValue(value_size_);
  size_t key_number = std::stoul(convert::ToString(keys[i]));
  if (transaction_size_ == 0) {
    TRACE_ASYNC_BEGIN("benchmark", "local_change_notification", key_number);
  }
  PutEntry(std::move(keys[i]), std::move(value),
           [this, i, key_number, keys = std::move(keys)]() mutable {
             if (transaction_size_ > 0 &&
                 (i % transaction_size_ == transaction_size_ - 1 ||
                  i + 1 == entry_count_)) {
               CommitAndRunNext(i, key_number, std::move(keys));
             } else {
               RunSingle(i + 1, std::move(keys));
             }
           });
}

void PutBenchmark::PutEntry(fidl::VectorPtr<uint8_t> key,
                            fidl::VectorPtr<uint8_t> value,
                            fit::function<void()> on_done) {
  auto trace_event_id = TRACE_NONCE();
  TRACE_ASYNC_BEGIN("benchmark", "put", trace_event_id);
  if (reference_strategy_ == PageDataGenerator::ReferenceStrategy::INLINE) {
    page_->Put(std::move(key), std::move(value),
               [this, trace_event_id,
                on_done = std::move(on_done)](ledger::Status status) {
                 if (QuitOnError(QuitLoopClosure(), status, "Page::Put")) {
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
      [this, trace_event_id, key = std::move(key),
       on_done = std::move(on_done)](ledger::Status status,
                                     ledger::ReferencePtr reference) mutable {
        if (QuitOnError(QuitLoopClosure(), status,
                        "Page::CreateReferenceFromVmo")) {
          return;
        }
        TRACE_ASYNC_END("benchmark", "create reference", trace_event_id);
        TRACE_ASYNC_BEGIN("benchmark", "put reference", trace_event_id);
        page_->PutReference(
            std::move(key), std::move(*reference), ledger::Priority::EAGER,
            [this, trace_event_id,
             on_done = std::move(on_done)](ledger::Status status) {
              if (QuitOnError(QuitLoopClosure(), status,
                              "Page::PutReference")) {
                return;
              }
              TRACE_ASYNC_END("benchmark", "put reference", trace_event_id);
              TRACE_ASYNC_END("benchmark", "put", trace_event_id);
              on_done();
            });
      });
}

void PutBenchmark::CommitAndRunNext(
    int i, size_t key_number, std::vector<fidl::VectorPtr<uint8_t>> keys) {
  TRACE_ASYNC_BEGIN("benchmark", "local_change_notification", key_number);
  TRACE_ASYNC_BEGIN("benchmark", "commit", i / transaction_size_);
  page_->Commit([this, i,
                 keys = std::move(keys)](ledger::Status status) mutable {
    if (QuitOnError(QuitLoopClosure(), status, "Page::Commit")) {
      return;
    }
    TRACE_ASYNC_END("benchmark", "commit", i / transaction_size_);
    TRACE_ASYNC_END("benchmark", "transaction", i / transaction_size_);

    if (i == entry_count_ - 1) {
      RunSingle(i + 1, std::move(keys));
      return;
    }
    page_->StartTransaction([this, i = i + 1, keys = std::move(keys)](
                                ledger::Status status) mutable {
      if (QuitOnError(QuitLoopClosure(), status, "Page::StartTransaction")) {
        return;
      }
      TRACE_ASYNC_BEGIN("benchmark", "transaction", i / transaction_size_);
      RunSingle(i, std::move(keys));
    });
  });
}

void PutBenchmark::ShutDown() {
  // Shut down the Ledger process first as it relies on |tmp_dir_| storage.
  test::KillLedgerProcess(&component_controller_);
  loop_->Quit();
}

fit::closure PutBenchmark::QuitLoopClosure() {
  return [this] { loop_->Quit(); };
}

}  // namespace benchmark
}  // namespace test
