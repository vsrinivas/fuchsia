// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/page_data_generator.h"

#include <lib/fit/function.h>

#include <memory>

#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/ledger/lib/rng/random.h"
#include "src/ledger/lib/vmo/strings.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {

namespace {

constexpr size_t kMaxInlineDataSize = ZX_CHANNEL_MAX_MSG_BYTES * 9 / 10;

bool LogOnError(Status status, absl::string_view description) {
  if (status != Status::OK) {
    LEDGER_LOG(ERROR) << description << " failed with status " << fidl::ToUnderlying(status) << ".";
    return true;
  }
  return false;
}

}  // namespace

PageDataGenerator::PageDataGenerator(Random* random) : generator_(random) {}

void PageDataGenerator::PutEntry(PagePtr* page, std::vector<uint8_t> key,
                                 std::vector<uint8_t> value, ReferenceStrategy ref_strategy,
                                 Priority priority, fit::function<void(Status)> callback) {
  if (ref_strategy == ReferenceStrategy::INLINE) {
    if (value.size() >= kMaxInlineDataSize) {
      LEDGER_LOG(ERROR) << "Value too large (" << value.size()
                        << ") to be put inline. Consider putting as reference instead.";
      callback(Status::IO_ERROR);
      return;
    }
    (*page)->PutWithPriority(std::move(key), std::move(value), priority);
    callback(Status::OK);
    return;
  }
  SizedVmo vmo;
  if (!VmoFromString(convert::ToStringView(value), &vmo)) {
    LogOnError(Status::IO_ERROR, "VmoFromString");
    callback(Status::IO_ERROR);
    return;
  }
  (*page)->CreateReferenceFromBuffer(
      std::move(vmo).ToTransport(),
      [page, key = std::move(key), priority, callback = std::move(callback)](
          fuchsia::ledger::Page_CreateReferenceFromBuffer_Result result) mutable {
        if (result.is_err()) {
          LogOnError(Status::IO_ERROR, "Page::CreateReferenceFromBuffer");
          callback(Status::IO_ERROR);
          return;
        }
        (*page)->PutReference(std::move(key), std::move(result.response().reference), priority);
        callback(Status::OK);
      });
}

void PageDataGenerator::Populate(PagePtr* page, std::vector<std::vector<uint8_t>> keys,
                                 size_t value_size, size_t transaction_size,
                                 ReferenceStrategy ref_strategy, Priority priority,
                                 fit::function<void(Status)> callback) {
  if (transaction_size == 0) {
    PutMultipleEntries(page, std::move(keys), value_size, ref_strategy, priority,
                       std::move(callback));
    return;
  }
  PutInTransaction(page, std::move(keys), 0, value_size, transaction_size, ref_strategy, priority,
                   std::move(callback));
}

void PageDataGenerator::PutInTransaction(PagePtr* page, std::vector<std::vector<uint8_t>> keys,
                                         size_t current_key_index, size_t value_size,
                                         size_t transaction_size, ReferenceStrategy ref_strategy,
                                         Priority priority, fit::function<void(Status)> callback) {
  if (current_key_index >= keys.size()) {
    (*page)->Sync([callback = std::move(callback)] { callback(Status::OK); });
    return;
  }
  size_t this_transaction_size = std::min(transaction_size, keys.size() - current_key_index);
  std::vector<std::vector<uint8_t>> partial_keys;
  std::move(keys.begin() + current_key_index,
            keys.begin() + current_key_index + this_transaction_size,
            std::back_inserter(partial_keys));

  (*page)->StartTransaction();
  PutMultipleEntries(
      page, std::move(partial_keys), value_size, ref_strategy, priority,
      [this, page, keys = std::move(keys), current_key_index, value_size, ref_strategy, priority,
       transaction_size, callback = std::move(callback)](Status status) mutable {
        if (LogOnError(status, "PutMultipleEntries")) {
          callback(status);
          return;
        }
        (*page)->Commit();
        PutInTransaction(page, std::move(keys), current_key_index + transaction_size, value_size,
                         transaction_size, ref_strategy, priority, std::move(callback));
      });
}

void PageDataGenerator::PutMultipleEntries(PagePtr* page, std::vector<std::vector<uint8_t>> keys,
                                           size_t value_size, ReferenceStrategy ref_strategy,
                                           Priority priority,
                                           fit::function<void(Status)> callback) {
  for (auto& key : keys) {
    std::vector<uint8_t> value = generator_.MakeValue(value_size);
    PutEntry(page, std::move(key), std::move(value), ref_strategy, priority,
             [](Status /*status*/) {});
  }
  (*page)->Sync([callback = std::move(callback)] { callback(Status::OK); });
}

}  // namespace ledger
