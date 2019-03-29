// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_usage_db.h"

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/zx/time.h>

#include "src/lib/fxl/strings/concatenate.h"
#include "peridot/lib/convert/convert.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/lock/lock.h"
#include "src/ledger/bin/storage/impl/data_serialization.h"
#include "src/ledger/bin/storage/public/iterator.h"

namespace ledger {
namespace {

constexpr fxl::StringView kOpenedPagePrefix = "opened/";

std::string GetKeyForOpenedPage(fxl::StringView ledger_name,
                                storage::PageIdView page_id) {
  FXL_DCHECK(page_id.size() == ::fuchsia::ledger::kPageIdSize);
  return fxl::Concatenate({kOpenedPagePrefix, ledger_name, page_id});
}

void GetPageFromOpenedRow(fxl::StringView row, std::string* ledger_name,
                          storage::PageId* page_id) {
  FXL_DCHECK(row.size() >
             ::fuchsia::ledger::kPageIdSize + kOpenedPagePrefix.size());
  size_t ledger_name_size =
      row.size() - ::fuchsia::ledger::kPageIdSize - kOpenedPagePrefix.size();
  *ledger_name =
      row.substr(kOpenedPagePrefix.size(), ledger_name_size).ToString();
  *page_id = row.substr(kOpenedPagePrefix.size() + ledger_name_size).ToString();
}

// An iterator over PageInfo.
// This class is a wrapper from a LevelDB iterator, deserializing the
// ExtendedStringView key-value pairs to PageInfo entries.
class PageInfoIterator final : public storage::Iterator<const PageInfo> {
 public:
  explicit PageInfoIterator(
      std::unique_ptr<storage::Iterator<const std::pair<
          convert::ExtendedStringView, convert::ExtendedStringView>>>
          it)
      : it_(std::move(it)) {
    PrepareEntry();
  }

  ~PageInfoIterator() override {}

  storage::Iterator<const PageInfo>& Next() override {
    it_->Next();
    PrepareEntry();
    return *this;
  }

  bool Valid() const override { return it_->Valid(); }

  storage::Status GetStatus() const override { return it_->GetStatus(); }

  const PageInfo& operator*() const override { return *(page_.get()); }

  const PageInfo* operator->() const override { return page_.get(); }

 private:
  // Updates `page_` with page info extracted from the current key-value in
  // `it_`.
  void PrepareEntry() {
    if (!Valid()) {
      page_.reset(nullptr);
      return;
    }
    page_ = std::make_unique<PageInfo>();

    const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>&
        key_value = **it_;
    GetPageFromOpenedRow(key_value.first, &(page_->ledger_name),
                         &(page_->page_id));
    page_->timestamp = storage::DeserializeData<zx::time_utc>(key_value.second);
  }

  std::unique_ptr<storage::Iterator<const std::pair<
      convert::ExtendedStringView, convert::ExtendedStringView>>>
      it_;

  // The current page info served by the iterator.
  std::unique_ptr<PageInfo> page_;
};
}  // namespace

PageUsageDb::PageUsageDb(timekeeper::Clock* clock,
                         std::unique_ptr<storage::Db> db)
    : clock_(clock), db_(std::move(db)) {}

PageUsageDb::~PageUsageDb() {}

storage::Status PageUsageDb::MarkPageOpened(
    coroutine::CoroutineHandler* handler, fxl::StringView ledger_name,
    storage::PageIdView page_id) {
  return Put(handler, GetKeyForOpenedPage(ledger_name, page_id),
             storage::SerializeData(PageInfo::kOpenedPageTimestamp));
}

storage::Status PageUsageDb::MarkPageClosed(
    coroutine::CoroutineHandler* handler, fxl::StringView ledger_name,
    storage::PageIdView page_id) {
  FXL_DCHECK(page_id.size() == ::fuchsia::ledger::kPageIdSize);
  zx::time_utc now;
  if (clock_->Now(&now) != ZX_OK) {
    return storage::Status::IO_ERROR;
  }
  return Put(handler, GetKeyForOpenedPage(ledger_name, page_id),
             storage::SerializeData(now));
}

storage::Status PageUsageDb::MarkPageEvicted(
    coroutine::CoroutineHandler* handler, fxl::StringView ledger_name,
    storage::PageIdView page_id) {
  return Delete(handler, GetKeyForOpenedPage(ledger_name, page_id));
}

storage::Status PageUsageDb::MarkAllPagesClosed(
    coroutine::CoroutineHandler* handler) {
  zx::time_utc now;
  if (clock_->Now(&now) != ZX_OK) {
    return storage::Status::IO_ERROR;
  }

  std::unique_ptr<storage::Iterator<const std::pair<
      convert::ExtendedStringView, convert::ExtendedStringView>>>
      rows;
  storage::Status db_status =
      db_->GetIteratorAtPrefix(handler, kOpenedPagePrefix, &rows);
  if (db_status != storage::Status::OK) {
    return db_status;
  }
  while (rows->Valid()) {
    if (storage::DeserializeData<zx::time_utc>((*rows)->second) ==
        PageInfo::kOpenedPageTimestamp) {
      // No need to deserialize the key.
      storage::Status status =
          Put(handler, (*rows)->first, storage::SerializeData(now));
      if (status != storage::Status::OK) {
        return status;
      }
    }
    rows->Next();
  }
  return storage::Status::OK;
}

storage::Status PageUsageDb::GetPages(
    coroutine::CoroutineHandler* handler,
    std::unique_ptr<storage::Iterator<const PageInfo>>* pages) {
  std::unique_ptr<storage::Iterator<const std::pair<
      convert::ExtendedStringView, convert::ExtendedStringView>>>
      it;
  storage::Status status =
      db_->GetIteratorAtPrefix(handler, kOpenedPagePrefix, &it);
  if (status != storage::Status::OK) {
    return status;
  }
  *pages = std::make_unique<PageInfoIterator>(std::move(it));
  return storage::Status::OK;
}

storage::Status PageUsageDb::Put(coroutine::CoroutineHandler* handler,
                                 fxl::StringView key, fxl::StringView value) {
  std::unique_ptr<storage::Db::Batch> batch;
  std::unique_ptr<lock::Lock> lock;
  // Used for serializing Put and Delete operations.
  if (lock::AcquireLock(handler, &serializer_, &lock) ==
      coroutine::ContinuationStatus::INTERRUPTED) {
    return storage::Status::INTERNAL_ERROR;
  }
  storage::Status status = db_->StartBatch(handler, &batch);
  if (status != storage::Status::OK) {
    return status;
  }
  status = batch->Put(handler, key, value);
  if (status != storage::Status::OK) {
    return status;
  }
  return batch->Execute(handler);
}

storage::Status PageUsageDb::Delete(coroutine::CoroutineHandler* handler,
                                    fxl::StringView key) {
  std::unique_ptr<storage::Db::Batch> batch;
  // Used for serializing Put and Delete operations.
  std::unique_ptr<lock::Lock> lock;
  if (lock::AcquireLock(handler, &serializer_, &lock) ==
      coroutine::ContinuationStatus::INTERRUPTED) {
    return storage::Status::INTERNAL_ERROR;
  }
  storage::Status status = db_->StartBatch(handler, &batch);
  if (status != storage::Status::OK) {
    return status;
  }
  status = batch->Delete(handler, key);
  if (status != storage::Status::OK) {
    return status;
  }
  return batch->Execute(handler);
}

}  // namespace ledger
