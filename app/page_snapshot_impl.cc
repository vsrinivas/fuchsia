// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>
#include <queue>
#include <vector>

#include "apps/ledger/app/page_snapshot_impl.h"

#include "apps/ledger/app/constants.h"
#include "apps/ledger/app/page_utils.h"
#include "apps/ledger/convert/convert.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/memory/ref_counted.h"
#include "lib/ftl/memory/ref_ptr.h"
#include "lib/ftl/tasks/task_runner.h"

namespace ledger {
namespace {
template <class T>
class Waiter : public ftl::RefCountedThreadSafe<Waiter<T>> {
 public:
  std::function<void(storage::Status, std::unique_ptr<T>)> NewCallback() {
    FTL_DCHECK(!finalized_);
    results_.push_back(std::unique_ptr<T>());
    std::function<void(storage::Status, std::unique_ptr<T>)> callback = [
      waiter_ref = ftl::RefPtr<Waiter<T>>(this), index = results_.size() - 1
    ](storage::Status status, std::unique_ptr<T> result) {
      waiter_ref->ReturnResult(index, status, std::move(result));
    };
    return callback;
  }

  void Finalize(std::function<void(storage::Status,
                                   std::vector<std::unique_ptr<T>>)> callback) {
    FTL_DCHECK(!finalized_) << "Waiter already finalized, can't finalize more!";
    result_callback_ = callback;
    finalized_ = true;
    ExecuteCallbackIfFinished();
  }

 private:
  void ReturnResult(int index,
                    storage::Status status,
                    std::unique_ptr<T> result) {
    if (result_status_ != storage::Status::OK)
      return;
    if (status != storage::Status::OK) {
      result_status_ = status;
      results_.clear();
      returned_results_ = 0;
      ExecuteCallbackIfFinished();
      return;
    }
    if (result) {
      results_[index].swap(result);
    }
    returned_results_++;
    ExecuteCallbackIfFinished();
  }

  void ExecuteCallbackIfFinished() {
    FTL_DCHECK(!finished_) << "Waiter already finished.";
    if (finalized_ && results_.size() == returned_results_) {
      result_callback_(result_status_, std::move(results_));
      finished_ = true;
    }
  }

  bool finished_ = false;
  bool finalized_ = false;
  size_t current_index_ = 0;

  size_t returned_results_ = 0;
  std::vector<std::unique_ptr<T>> results_;
  storage::Status result_status_ = storage::Status::OK;

  std::function<void(storage::Status, std::vector<std::unique_ptr<T>>)>
      result_callback_;
};

}  // namespace

PageSnapshotImpl::PageSnapshotImpl(
    storage::PageStorage* page_storage,
    std::unique_ptr<storage::CommitContents> contents)
    : page_storage_(page_storage), contents_(std::move(contents)) {}

PageSnapshotImpl::~PageSnapshotImpl() {}

void PageSnapshotImpl::GetEntries(mojo::Array<uint8_t> key_prefix,
                                  mojo::Array<uint8_t> token,
                                  const GetEntriesCallback& callback) {
  std::unique_ptr<storage::Iterator<const storage::Entry>> it =
      contents_->find(key_prefix);
  ftl::RefPtr<Waiter<const storage::Object>> waiter(
      ftl::AdoptRef(new Waiter<const storage::Object>()));
  mojo::Array<EntryPtr> entries = mojo::Array<EntryPtr>::New(0);

  while (it->Valid() &&
         convert::ExtendedStringView((*it)->key).substr(0, key_prefix.size()) ==
             convert::ExtendedStringView(key_prefix)) {
    EntryPtr entry = Entry::New();
    entry->key = convert::ToArray((*it)->key);
    entries.push_back(entry.Pass());

    page_storage_->GetObject(
        (*it)->object_id,
        [object_callback = waiter->NewCallback()](
            storage::Status status,
            std::unique_ptr<const storage::Object> object) mutable {
          object_callback(status, std::move(object));
        });
    it->Next();
  }

  std::function<void(storage::Status,
                     std::vector<std::unique_ptr<const storage::Object>>)>
      result_callback = ftl::MakeCopyable([
        callback, entry_list = std::move(entries)
      ](storage::Status status,
        std::vector<std::unique_ptr<const storage::Object>> results) mutable {

        if (status != storage::Status::OK) {
          FTL_LOG(ERROR) << "PageSnapshotImpl::GetEntries error while reading.";
          callback.Run(Status::IO_ERROR, nullptr, nullptr);
          return;
        }

        for (size_t i = 0; i < results.size(); i++) {
          ftl::StringView object_contents;

          storage::Status read_status = results[i]->GetData(&object_contents);
          if (read_status != storage::Status::OK) {
            callback.Run(Status::IO_ERROR, nullptr, nullptr);
            return;
          }

          entry_list[i]->value = convert::ToArray(object_contents);
        }
        callback.Run(Status::OK, std::move(entry_list), nullptr);
      });
  waiter->Finalize(result_callback);
}

void PageSnapshotImpl::GetKeys(mojo::Array<uint8_t> key_prefix,
                               mojo::Array<uint8_t> token,
                               const GetKeysCallback& callback) {
  std::unique_ptr<storage::Iterator<const storage::Entry>> it =
      contents_->find(key_prefix);
  mojo::Array<mojo::Array<uint8_t>> keys =
      mojo::Array<mojo::Array<uint8_t>>::New(0);

  while (it->Valid() &&
         convert::ExtendedStringView((*it)->key).substr(0, key_prefix.size()) ==
             convert::ExtendedStringView(key_prefix)) {
    keys.push_back(convert::ToArray((*it)->key));
    it->Next();
  }
  callback.Run(Status::OK, keys.Pass(), nullptr);
}

void PageSnapshotImpl::Get(mojo::Array<uint8_t> key,
                           const GetCallback& callback) {
  std::unique_ptr<storage::Iterator<const storage::Entry>> it =
      contents_->find(key);
  if (!it->Valid() ||
      convert::ExtendedStringView((*it)->key) !=
          convert::ExtendedStringView(key)) {
    callback.Run(Status::KEY_NOT_FOUND, nullptr);
    return;
  }
  PageUtils::GetReferenceAsValuePtr(page_storage_, (*it)->object_id,
                                    [callback](Status status, ValuePtr value) {
                                      callback.Run(status, std::move(value));
                                    });
}

void PageSnapshotImpl::GetPartial(mojo::Array<uint8_t> key,
                                  int64_t offset,
                                  int64_t max_size,
                                  const GetPartialCallback& callback) {
  std::unique_ptr<storage::Iterator<const storage::Entry>> it =
      contents_->find(key);
  if (!it->Valid() ||
      convert::ExtendedStringView((*it)->key) !=
          convert::ExtendedStringView(key)) {
    callback.Run(Status::KEY_NOT_FOUND, mojo::ScopedSharedBufferHandle());
    return;
  }
  PageUtils::GetPartialReferenceAsBuffer(
      page_storage_, (*it)->object_id, offset, max_size,
      [callback](ledger::Status status, mojo::ScopedSharedBufferHandle handle) {
        callback.Run(status, std::move(handle));
      });
}

}  // namespace ledger
