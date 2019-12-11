// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_usage_db.h"

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/zx/time.h>

#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/storage/impl/data_serialization.h"
#include "src/ledger/bin/storage/public/iterator.h"
#include "src/ledger/bin/synchronization/lock.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/logging/logging.h"
#include "third_party/abseil-cpp/absl/strings/str_cat.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {
namespace {

constexpr absl::string_view kOpenedPagePrefix = "opened/";

std::string GetKeyForOpenedPage(absl::string_view ledger_name, storage::PageIdView page_id) {
  LEDGER_DCHECK(page_id.size() == ::fuchsia::ledger::PAGE_ID_SIZE);
  return absl::StrCat(kOpenedPagePrefix, ledger_name, page_id);
}

void GetPageFromOpenedRow(absl::string_view row, std::string* ledger_name,
                          storage::PageId* page_id) {
  LEDGER_DCHECK(row.size() > ::fuchsia::ledger::PAGE_ID_SIZE + kOpenedPagePrefix.size());
  size_t ledger_name_size = row.size() - ::fuchsia::ledger::PAGE_ID_SIZE - kOpenedPagePrefix.size();
  *ledger_name = convert::ToString(row.substr(kOpenedPagePrefix.size(), ledger_name_size));
  *page_id = convert::ToString(row.substr(kOpenedPagePrefix.size() + ledger_name_size));
}

// An iterator over PageInfo.
// This class is a wrapper from a LevelDB iterator, deserializing the
// ExtendedStringView key-value pairs to PageInfo entries.
class PageInfoIterator final : public storage::Iterator<const PageInfo> {
 public:
  explicit PageInfoIterator(
      std::unique_ptr<storage::Iterator<
          const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>>>
          it)
      : it_(std::move(it)) {
    PrepareEntry();
  }

  ~PageInfoIterator() override = default;

  storage::Iterator<const PageInfo>& Next() override {
    it_->Next();
    PrepareEntry();
    return *this;
  }

  bool Valid() const override { return it_->Valid(); }

  Status GetStatus() const override { return it_->GetStatus(); }

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

    const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>& key_value = **it_;
    GetPageFromOpenedRow(key_value.first, &(page_->ledger_name), &(page_->page_id));
    page_->timestamp = storage::DeserializeData<zx::time_utc>(key_value.second);
  }

  std::unique_ptr<
      storage::Iterator<const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>>>
      it_;

  // The current page info served by the iterator.
  std::unique_ptr<PageInfo> page_;
};

// If the given |status| is not |OK| or |INTERRUPTED|, logs an error message on
// failure to initialize. Returns true in case of error; false otherwise.
bool LogOnInitializationError(absl::string_view operation_description, Status status) {
  if (status != Status::OK) {
    if (status != Status::INTERRUPTED) {
      LEDGER_LOG(ERROR) << operation_description << " failed because of initialization error: "
                        << fidl::ToUnderlying(status);
    }
    return true;
  }
  return false;
}
}  // namespace

PageUsageDb::PageUsageDb(Environment* environment, std::unique_ptr<storage::Db> db)
    : clock_(environment->clock()),
      db_(std::move(db)),
      initialization_completer_(environment->dispatcher()) {}

PageUsageDb::~PageUsageDb() = default;

Status PageUsageDb::Init(coroutine::CoroutineHandler* handler) {
  if (initialization_called_) {
    return SyncWaitUntilDone(handler, &initialization_completer_);
  }
  initialization_called_ = true;
  Status status = MarkAllPagesClosed(handler);
  if (status == Status::INTERRUPTED) {
    return Status::INTERRUPTED;
  }
  initialization_completer_.Complete(status);
  return status;
}

Status PageUsageDb::MarkPageOpened(coroutine::CoroutineHandler* handler,
                                   absl::string_view ledger_name, storage::PageIdView page_id) {
  Status status = Init(handler);
  if (LogOnInitializationError("MarkPageOpened", status)) {
    return status;
  }
  return Put(handler, GetKeyForOpenedPage(ledger_name, page_id),
             storage::SerializeData(PageInfo::kOpenedPageTimestamp));
}

Status PageUsageDb::MarkPageClosed(coroutine::CoroutineHandler* handler,
                                   absl::string_view ledger_name, storage::PageIdView page_id) {
  Status status = Init(handler);
  if (LogOnInitializationError("MarkPageClosed", status)) {
    return status;
  }
  LEDGER_DCHECK(page_id.size() == ::fuchsia::ledger::PAGE_ID_SIZE);
  zx::time_utc now;
  if (clock_->Now(&now) != ZX_OK) {
    return Status::IO_ERROR;
  }
  return Put(handler, GetKeyForOpenedPage(ledger_name, page_id), storage::SerializeData(now));
}

Status PageUsageDb::MarkPageEvicted(coroutine::CoroutineHandler* handler,
                                    absl::string_view ledger_name, storage::PageIdView page_id) {
  Status status = Init(handler);
  if (LogOnInitializationError("TryEvictPage", status)) {
    return status;
  }
  return Delete(handler, GetKeyForOpenedPage(ledger_name, page_id));
}

Status PageUsageDb::MarkAllPagesClosed(coroutine::CoroutineHandler* handler) {
  zx::time_utc now;
  if (clock_->Now(&now) != ZX_OK) {
    return Status::IO_ERROR;
  }

  std::unique_ptr<
      storage::Iterator<const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>>>
      rows;
  Status db_status = db_->GetIteratorAtPrefix(handler, kOpenedPagePrefix, &rows);
  if (db_status != Status::OK) {
    return db_status;
  }
  while (rows->Valid()) {
    if (storage::DeserializeData<zx::time_utc>((*rows)->second) == PageInfo::kOpenedPageTimestamp) {
      // No need to deserialize the key.
      RETURN_ON_ERROR(Put(handler, (*rows)->first, storage::SerializeData(now)));
    }
    rows->Next();
  }
  return Status::OK;
}

Status PageUsageDb::GetPages(coroutine::CoroutineHandler* handler,
                             std::unique_ptr<storage::Iterator<const PageInfo>>* pages) {
  Status status = Init(handler);
  if (LogOnInitializationError("TryEvictPages", status)) {
    return status;
  }
  std::unique_ptr<
      storage::Iterator<const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>>>
      it;
  RETURN_ON_ERROR(db_->GetIteratorAtPrefix(handler, kOpenedPagePrefix, &it));
  *pages = std::make_unique<PageInfoIterator>(std::move(it));
  return Status::OK;
}

bool PageUsageDb::IsInitialized() { return initialization_completer_.IsCompleted(); }

Status PageUsageDb::Put(coroutine::CoroutineHandler* handler, absl::string_view key,
                        absl::string_view value) {
  std::unique_ptr<storage::Db::Batch> batch;
  std::unique_ptr<lock::Lock> lock;
  // Used for serializing Put and Delete operations.
  if (lock::AcquireLock(handler, &serializer_, &lock) ==
      coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERNAL_ERROR;
  }
  RETURN_ON_ERROR(db_->StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->Put(handler, key, value));
  return batch->Execute(handler);
}

Status PageUsageDb::Delete(coroutine::CoroutineHandler* handler, absl::string_view key) {
  std::unique_ptr<storage::Db::Batch> batch;
  // Used for serializing Put and Delete operations.
  std::unique_ptr<lock::Lock> lock;
  if (lock::AcquireLock(handler, &serializer_, &lock) ==
      coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERNAL_ERROR;
  }
  RETURN_ON_ERROR(db_->StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->Delete(handler, key));
  return batch->Execute(handler);
}

}  // namespace ledger
