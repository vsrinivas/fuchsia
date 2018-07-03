// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/page_data_generator.h"

#include <memory>

#include <lib/callback/waiter.h>
#include <lib/fit/function.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/memory/ref_ptr.h>
#include <lib/fxl/random/rand.h>

#include "peridot/lib/convert/convert.h"

namespace {

constexpr size_t kMaxInlineDataSize = ZX_CHANNEL_MAX_MSG_BYTES * 9 / 10;

bool LogOnError(ledger::Status status, fxl::StringView description) {
  if (status != ledger::Status::OK) {
    FXL_LOG(ERROR) << description << " failed with status " << status << ".";
    return true;
  }
  return false;
}

}  // namespace

namespace test {
namespace benchmark {

PageDataGenerator::PageDataGenerator() : generator_(fxl::RandUint64()){};

void PageDataGenerator::PutEntry(ledger::PagePtr* page,
                                 fidl::VectorPtr<uint8_t> key,
                                 fidl::VectorPtr<uint8_t> value,
                                 ReferenceStrategy ref_strategy,
                                 ledger::Priority priority,
                                 fit::function<void(ledger::Status)> callback) {
  if (ref_strategy == ReferenceStrategy::INLINE) {
    if (value->size() >= kMaxInlineDataSize) {
      FXL_LOG(ERROR)
          << "Value too large (" << value->size()
          << ") to be put inline. Consider putting as reference instead.";
      callback(ledger::Status::IO_ERROR);
      return;
    }
    (*page)->PutWithPriority(
        std::move(key), std::move(value), priority,
        [callback = std::move(callback)](ledger::Status status) {
          LogOnError(status, "Page::PutWithPriority");
          callback(status);
        });
    return;
  }
  fsl::SizedVmo vmo;
  if (!fsl::VmoFromString(convert::ToStringView(value), &vmo)) {
    LogOnError(ledger::Status::IO_ERROR, "fsl::VmoFromString");
    callback(ledger::Status::IO_ERROR);
    return;
  }
  (*page)->CreateReferenceFromVmo(
      std::move(vmo).ToTransport(),
      [page, key = std::move(key), priority, callback = std::move(callback)](
          ledger::Status status, ledger::ReferencePtr reference) mutable {
        if (LogOnError(status, "Page::CreateReferenceFromVmo")) {
          callback(status);
          return;
        }
        (*page)->PutReference(
            std::move(key), std::move(*reference), priority,
            [callback = std::move(callback)](ledger::Status status) {
              LogOnError(status, "Page::PutReference");
              callback(status);
            });
      });
}

void PageDataGenerator::Populate(ledger::PagePtr* page,
                                 std::vector<fidl::VectorPtr<uint8_t>> keys,
                                 size_t value_size, size_t transaction_size,
                                 ReferenceStrategy ref_strategy,
                                 ledger::Priority priority,
                                 fit::function<void(ledger::Status)> callback) {
  if (transaction_size == 0) {
    PutMultipleEntries(page, std::move(keys), value_size, ref_strategy,
                       priority, std::move(callback));
    return;
  }
  PutInTransaction(page, std::move(keys), 0, value_size, transaction_size,
                   ref_strategy, priority, std::move(callback));
}

void PageDataGenerator::PutInTransaction(
    ledger::PagePtr* page, std::vector<fidl::VectorPtr<uint8_t>> keys,
    size_t current_key_index, size_t value_size, size_t transaction_size,
    ReferenceStrategy ref_strategy, ledger::Priority priority,
    fit::function<void(ledger::Status)> callback) {
  if (current_key_index >= keys.size()) {
    callback(ledger::Status::OK);
    return;
  }
  size_t this_transaction_size =
      std::min(transaction_size, keys.size() - current_key_index);
  std::vector<fidl::VectorPtr<uint8_t>> partial_keys;
  std::move(keys.begin() + current_key_index,
            keys.begin() + current_key_index + this_transaction_size,
            std::back_inserter(partial_keys));

  (*page)->StartTransaction([this, page, partial_keys = std::move(partial_keys),
                             keys = std::move(keys), current_key_index,
                             transaction_size, value_size, ref_strategy,
                             priority, callback = std::move(callback)](
                                ledger::Status status) mutable {
    if (LogOnError(status, "Page::StartTransaction")) {
      callback(status);
      return;
    }
    PutMultipleEntries(
        page, std::move(partial_keys), value_size, ref_strategy, priority,
        [this, page, keys = std::move(keys), current_key_index, value_size,
         ref_strategy, priority, transaction_size,
         callback = std::move(callback)](ledger::Status status) mutable {
          if (LogOnError(status, "PutMultipleEntries")) {
            callback(status);
            return;
          }
          (*page)->Commit(
              [this, page, keys = std::move(keys), current_key_index,
               value_size, ref_strategy, transaction_size, priority,
               callback = std::move(callback)](ledger::Status status) mutable {
                if (LogOnError(status, "Page::Commit")) {
                  callback(status);
                  return;
                }
                PutInTransaction(page, std::move(keys),
                                 current_key_index + transaction_size,
                                 value_size, transaction_size, ref_strategy,
                                 priority, std::move(callback));
              });
        });
  });
}

void PageDataGenerator::PutMultipleEntries(
    ledger::PagePtr* page, std::vector<fidl::VectorPtr<uint8_t>> keys,
    size_t value_size, ReferenceStrategy ref_strategy,
    ledger::Priority priority, fit::function<void(ledger::Status)> callback) {
  auto waiter = fxl::MakeRefCounted<callback::StatusWaiter<ledger::Status>>(
      ledger::Status::OK);
  for (auto& key : keys) {
    fidl::VectorPtr<uint8_t> value = generator_.MakeValue(value_size);
    PutEntry(page, std::move(key), std::move(value), ref_strategy, priority,
             waiter->NewCallback());
  }
  waiter->Finalize(std::move(callback));
}

}  // namespace benchmark
}  // namespace test
