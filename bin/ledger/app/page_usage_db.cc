// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/page_usage_db.h"

#include <zx/time.h>

#include "lib/fxl/strings/concatenate.h"
#include "peridot/bin/ledger/app/constants.h"
#include "peridot/bin/ledger/lock/lock.h"
#include "peridot/bin/ledger/storage/impl/number_serialization.h"
#include "peridot/bin/ledger/storage/public/iterator.h"
#include "peridot/lib/convert/convert.h"

namespace ledger {
namespace {

constexpr fxl::StringView kOpenedPagePrefix = "opened/";

std::string GetKeyForOpenedPage(fxl::StringView ledger_name,
                                storage::PageIdView page_id) {
  FXL_DCHECK(page_id.size() == kPageIdSize);
  return fxl::Concatenate({kOpenedPagePrefix, ledger_name, page_id});
}

void GetPageFromOpenedRow(fxl::StringView row, std::string* ledger_name,
                          storage::PageId* page_id) {
  FXL_DCHECK(row.size() > kPageIdSize + kOpenedPagePrefix.size());
  size_t ledger_name_size = row.size() - kPageIdSize - kOpenedPagePrefix.size();
  *ledger_name =
      row.substr(kOpenedPagePrefix.size(), ledger_name_size).ToString();
  *page_id = row.substr(kOpenedPagePrefix.size() + ledger_name_size).ToString();
}

// An iterator over PageUsageDb::PageInfo.
// This class is a wrapper from a LevelDB iterator, deserializing the
// ExtendedStringView key-value pairs to PageInfo entries.
class PageInfoIterator : public storage::Iterator<const PageUsageDb::PageInfo> {
 public:
  explicit PageInfoIterator(
      std::unique_ptr<storage::Iterator<const std::pair<
          convert::ExtendedStringView, convert::ExtendedStringView>>>
          it)
      : it_(std::move(it)) {
    PrepareEntry();
  }

  ~PageInfoIterator() override {}

  storage::Iterator<const PageUsageDb::PageInfo>& Next() override {
    it_->Next();
    PrepareEntry();
    return *this;
  }

  bool Valid() const override { return it_->Valid(); }

  storage::Status GetStatus() const override { return it_->GetStatus(); }

  const PageUsageDb::PageInfo& operator*() const override {
    return *(page_.get());
  }

  const PageUsageDb::PageInfo* operator->() const override {
    return page_.get();
  }

 private:
  // Updates `page_` with page info extracted from the current key-value in
  // `it_`.
  void PrepareEntry() {
    if (!Valid()) {
      page_.reset(nullptr);
      return;
    }
    page_ = std::make_unique<PageUsageDb::PageInfo>();

    const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>&
        key_value = **it_;
    GetPageFromOpenedRow(key_value.first, &(page_->ledger_name),
                         &(page_->page_id));
    page_->timestamp =
        zx::time(storage::DeserializeNumber<zx_time_t>(key_value.second));
  }

  std::unique_ptr<storage::Iterator<const std::pair<
      convert::ExtendedStringView, convert::ExtendedStringView>>>
      it_;

  // The current page info served by the iterator.
  std::unique_ptr<PageUsageDb::PageInfo> page_;
};
}  // namespace

PageUsageDb::PageUsageDb(async_dispatcher_t* dispatcher,
                         ledger::DetachedPath db_path)
    : db_(dispatcher, std::move(db_path)) {}

PageUsageDb::~PageUsageDb() {}

Status PageUsageDb::Init() { return PageUtils::ConvertStatus(db_.Init()); }

Status PageUsageDb::MarkPageOpened(coroutine::CoroutineHandler* handler,
                                   fxl::StringView ledger_name,
                                   storage::PageIdView page_id) {
  return Put(handler, GetKeyForOpenedPage(ledger_name, page_id),
             storage::SerializeNumber<zx_time_t>(0));
}

Status PageUsageDb::MarkPageClosed(coroutine::CoroutineHandler* handler,
                                   fxl::StringView ledger_name,
                                   storage::PageIdView page_id) {
  FXL_DCHECK(page_id.size() == kPageIdSize);
  zx::time_utc now;
  if (zx::clock::get(&now) != ZX_OK) {
    return Status::IO_ERROR;
  }
  return Put(handler, GetKeyForOpenedPage(ledger_name, page_id),
             storage::SerializeNumber(now.get()));
}

Status PageUsageDb::MarkPageEvicted(coroutine::CoroutineHandler* handler,
                                    fxl::StringView ledger_name,
                                    storage::PageIdView page_id) {
  return Delete(handler, GetKeyForOpenedPage(ledger_name, page_id));
}

Status PageUsageDb::MarkAllPagesClosed(coroutine::CoroutineHandler* handler) {
  zx::time_utc now;
  if (zx::clock::get(&now) != ZX_OK) {
    return Status::IO_ERROR;
  }

  std::unique_ptr<storage::Iterator<const std::pair<
      convert::ExtendedStringView, convert::ExtendedStringView>>>
      rows;
  storage::Status db_status =
      db_.GetIteratorAtPrefix(handler, kOpenedPagePrefix, &rows);
  if (db_status != storage::Status::OK) {
    return PageUtils::ConvertStatus(db_status);
  }
  while (rows->Valid()) {
    if (storage::DeserializeNumber<zx_time_t>((*rows)->second) == 0) {
      // No need to deserialize the key.
      Status status =
          Put(handler, (*rows)->first, storage::SerializeNumber(now.get()));
      if (status != Status::OK) {
        return status;
      }
    }
    rows->Next();
  }
  return Status::OK;
}

Status PageUsageDb::GetPages(
    coroutine::CoroutineHandler* handler,
    std::unique_ptr<storage::Iterator<const PageUsageDb::PageInfo>>* pages) {
  std::unique_ptr<storage::Iterator<const std::pair<
      convert::ExtendedStringView, convert::ExtendedStringView>>>
      it;
  storage::Status status =
      db_.GetIteratorAtPrefix(handler, kOpenedPagePrefix, &it);
  if (status != storage::Status::OK) {
    return PageUtils::ConvertStatus(status);
  }
  *pages = std::make_unique<PageInfoIterator>(std::move(it));
  return Status::OK;
}

Status PageUsageDb::Put(coroutine::CoroutineHandler* handler,
                        fxl::StringView key, fxl::StringView value) {
  std::unique_ptr<storage::Db::Batch> batch;
  std::unique_ptr<lock::Lock> lock;
  // Used for serializing Put and Delete operations.
  if (lock::AcquireLock(handler, &serializer_, &lock) ==
      coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERNAL_ERROR;
  }
  storage::Status status = db_.StartBatch(handler, &batch);
  if (status != storage::Status::OK) {
    return PageUtils::ConvertStatus(status);
  }
  status = batch->Put(handler, key, value);
  if (status != storage::Status::OK) {
    return PageUtils::ConvertStatus(status);
  }
  return PageUtils::ConvertStatus(batch->Execute(handler));
}

Status PageUsageDb::Delete(coroutine::CoroutineHandler* handler,
                           fxl::StringView key) {
  std::unique_ptr<storage::Db::Batch> batch;
  // Used for serializing Put and Delete operations.
  std::unique_ptr<lock::Lock> lock;
  if (lock::AcquireLock(handler, &serializer_, &lock) ==
      coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERNAL_ERROR;
  }
  storage::Status status = db_.StartBatch(handler, &batch);
  if (status != storage::Status::OK) {
    return PageUtils::ConvertStatus(status);
  }
  status = batch->Delete(handler, key);
  if (status != storage::Status::OK) {
    return PageUtils::ConvertStatus(status);
  }
  return PageUtils::ConvertStatus(batch->Execute(handler));
}

}  // namespace ledger
