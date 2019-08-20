// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_usage_db.h"

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/zx/time.h>

#include "peridot/lib/convert/convert.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/storage/impl/data_serialization.h"
#include "src/ledger/bin/storage/public/iterator.h"
#include "src/ledger/bin/synchronization/lock.h"
#include "src/lib/files/directory.h"
#include "src/lib/fxl/strings/concatenate.h"

namespace ledger {
namespace {

constexpr fxl::StringView kOpenedPagePrefix = "opened/";

std::string GetKeyForOpenedPage(fxl::StringView ledger_name, storage::PageIdView page_id) {
  FXL_DCHECK(page_id.size() == ::fuchsia::ledger::PAGE_ID_SIZE);
  return fxl::Concatenate({kOpenedPagePrefix, ledger_name, page_id});
}

void GetPageFromOpenedRow(fxl::StringView row, std::string* ledger_name, storage::PageId* page_id) {
  FXL_DCHECK(row.size() > ::fuchsia::ledger::PAGE_ID_SIZE + kOpenedPagePrefix.size());
  size_t ledger_name_size = row.size() - ::fuchsia::ledger::PAGE_ID_SIZE - kOpenedPagePrefix.size();
  *ledger_name = row.substr(kOpenedPagePrefix.size(), ledger_name_size).ToString();
  *page_id = row.substr(kOpenedPagePrefix.size() + ledger_name_size).ToString();
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

  ~PageInfoIterator() override {}

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
bool LogOnInitializationError(fxl::StringView operation_description, Status status) {
  if (status != Status::OK) {
    if (status != Status::INTERRUPTED) {
      FXL_LOG(ERROR) << operation_description
                     << " failed because of initialization error: " << fidl::ToUnderlying(status);
    }
    return true;
  }
  return false;
}
}  // namespace

PageUsageDb::Completer::Completer() {}

PageUsageDb::Completer::~Completer() {
  // We should not call the callbacks: they are SyncCall callbacks, so when we
  // drop them the caller will receive |INTERRUPTED|.
}

void PageUsageDb::Completer::Complete(Status status) {
  FXL_DCHECK(!completed_);
  // If we get |INTERRUPTED| here, it means the caller did not return as soon as
  // it received |INTERRUPTED|.
  FXL_DCHECK(status != Status::INTERRUPTED);
  CallCallbacks(status);
}

Status PageUsageDb::Completer::WaitUntilDone(coroutine::CoroutineHandler* handler) {
  if (completed_) {
    return status_;
  }

  auto sync_call_status = coroutine::SyncCall(handler, [this](fit::closure callback) {
    // SyncCall finishes its execution when the given |callback| is called.
    // To block the termination of |SyncCall| (and of |WaitUntilDone|), here
    // we push this |callback| in the vector of |callbacks_|. Once
    // |Complete| is called, we will call all of these callbacks, which will
    // eventually unblock all pending |WaitUntilDone| calls.
    callbacks_.push_back(std::move(callback));
  });
  if (sync_call_status == coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERRUPTED;
  }
  return status_;
}

bool PageUsageDb::Completer::IsCompleted() { return completed_; }

void PageUsageDb::Completer::CallCallbacks(Status status) {
  if (completed_) {
    return;
  }
  completed_ = true;
  status_ = status;
  // We need to move the callbacks in the stack since calling any of the
  // them might lead to the deletion of this object, invalidating callbacks_.
  std::vector<fit::closure> callbacks = std::move(callbacks_);
  callbacks_.clear();
  for (const auto& callback : callbacks) {
    callback();
  }
}

void PageUsageDb::Completer::Cancel() {
  FXL_DCHECK(!completed_);
  completed_ = true;
  status_ = Status::INTERRUPTED;
  callbacks_.clear();
}

PageUsageDb::PageUsageDb(timekeeper::Clock* clock, storage::DbFactory* db_factory,
                         DetachedPath db_path)
    : clock_(clock),
      db_factory_(db_factory),
      db_path_(db_path.SubPath(kPageUsageDbSerializationVersion)) {}

PageUsageDb::~PageUsageDb() {}

Status PageUsageDb::Init(coroutine::CoroutineHandler* handler) {
  // Initializing the DB and marking pages as closed are slow operations and we
  // shouldn't wait for them to finish, before returning from initialization:
  // Start these operations and finalize the initialization completer when done.
  if (!files::CreateDirectoryAt(db_path_.root_fd(), db_path_.path())) {
    initialization_completer_.Complete(Status::IO_ERROR);
    return Status::IO_ERROR;
  }
  Status status;
  if (coroutine::SyncCall(
          handler,
          [this](fit::function<void(Status, std::unique_ptr<storage::Db>)> callback) {
            db_factory_->GetOrCreateDb(
                std::move(db_path_), storage::DbFactory::OnDbNotFound::CREATE, std::move(callback));
          },
          &status, &db_) == coroutine::ContinuationStatus::INTERRUPTED) {
    initialization_completer_.Cancel();
    return Status::INTERRUPTED;
  }
  if (status != Status::OK) {
    initialization_completer_.Complete(status);
    return status;
  }
  status = MarkAllPagesClosed(handler);
  if (status == Status::INTERRUPTED) {
    initialization_completer_.Cancel();
    return Status::INTERRUPTED;
  }
  initialization_completer_.Complete(status);
  return status;
}

Status PageUsageDb::MarkPageOpened(coroutine::CoroutineHandler* handler,
                                   fxl::StringView ledger_name, storage::PageIdView page_id) {
  Status status = initialization_completer_.WaitUntilDone(handler);
  if (LogOnInitializationError("MarkPageOpened", status)) {
    return status;
  }
  return Put(handler, GetKeyForOpenedPage(ledger_name, page_id),
             storage::SerializeData(PageInfo::kOpenedPageTimestamp));
}

Status PageUsageDb::MarkPageClosed(coroutine::CoroutineHandler* handler,
                                   fxl::StringView ledger_name, storage::PageIdView page_id) {
  Status status = initialization_completer_.WaitUntilDone(handler);
  if (LogOnInitializationError("MarkPageClosed", status)) {
    return status;
  }
  FXL_DCHECK(page_id.size() == ::fuchsia::ledger::PAGE_ID_SIZE);
  zx::time_utc now;
  if (clock_->Now(&now) != ZX_OK) {
    return Status::IO_ERROR;
  }
  return Put(handler, GetKeyForOpenedPage(ledger_name, page_id), storage::SerializeData(now));
}

Status PageUsageDb::MarkPageEvicted(coroutine::CoroutineHandler* handler,
                                    fxl::StringView ledger_name, storage::PageIdView page_id) {
  Status status = initialization_completer_.WaitUntilDone(handler);
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
      Status status = Put(handler, (*rows)->first, storage::SerializeData(now));
      if (status != Status::OK) {
        return status;
      }
    }
    rows->Next();
  }
  return Status::OK;
}

Status PageUsageDb::GetPages(coroutine::CoroutineHandler* handler,
                             std::unique_ptr<storage::Iterator<const PageInfo>>* pages) {
  Status status = initialization_completer_.WaitUntilDone(handler);
  if (LogOnInitializationError("TryEvictPages", status)) {
    return status;
  }
  std::unique_ptr<
      storage::Iterator<const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>>>
      it;
  status = db_->GetIteratorAtPrefix(handler, kOpenedPagePrefix, &it);
  if (status != Status::OK) {
    return status;
  }
  *pages = std::make_unique<PageInfoIterator>(std::move(it));
  return Status::OK;
}

bool PageUsageDb::IsInitialized() { return initialization_completer_.IsCompleted(); }

Status PageUsageDb::Put(coroutine::CoroutineHandler* handler, fxl::StringView key,
                        fxl::StringView value) {
  std::unique_ptr<storage::Db::Batch> batch;
  std::unique_ptr<lock::Lock> lock;
  // Used for serializing Put and Delete operations.
  if (lock::AcquireLock(handler, &serializer_, &lock) ==
      coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERNAL_ERROR;
  }
  Status status = db_->StartBatch(handler, &batch);
  if (status != Status::OK) {
    return status;
  }
  status = batch->Put(handler, key, value);
  if (status != Status::OK) {
    return status;
  }
  return batch->Execute(handler);
}

Status PageUsageDb::Delete(coroutine::CoroutineHandler* handler, fxl::StringView key) {
  std::unique_ptr<storage::Db::Batch> batch;
  // Used for serializing Put and Delete operations.
  std::unique_ptr<lock::Lock> lock;
  if (lock::AcquireLock(handler, &serializer_, &lock) ==
      coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERNAL_ERROR;
  }
  Status status = db_->StartBatch(handler, &batch);
  if (status != Status::OK) {
    return status;
  }
  status = batch->Delete(handler, key);
  if (status != Status::OK) {
    return status;
  }
  return batch->Execute(handler);
}

}  // namespace ledger
