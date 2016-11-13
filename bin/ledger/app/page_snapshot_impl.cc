// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>
#include <queue>
#include <vector>

#include "apps/ledger/src/app/page_snapshot_impl.h"

#include "apps/ledger/src/app/constants.h"
#include "apps/ledger/src/app/page_utils.h"
#include "apps/ledger/src/callback/waiter.h"
#include "apps/ledger/src/convert/convert.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/memory/ref_counted.h"
#include "lib/ftl/memory/ref_ptr.h"
#include "lib/ftl/tasks/task_runner.h"

namespace ledger {
PageSnapshotImpl::PageSnapshotImpl(
    storage::PageStorage* page_storage,
    std::unique_ptr<storage::CommitContents> contents)
    : page_storage_(page_storage), contents_(std::move(contents)) {}

PageSnapshotImpl::~PageSnapshotImpl() {}

void PageSnapshotImpl::GetEntries(fidl::Array<uint8_t> key_prefix,
                                  fidl::Array<uint8_t> token,
                                  const GetEntriesCallback& callback) {
  std::unique_ptr<storage::Iterator<const storage::Entry>> it =
      contents_->find(key_prefix);
  auto waiter =
      callback::Waiter<storage::Status, const storage::Object>::Create(
          storage::Status::OK);
  fidl::Array<EntryPtr> entries = fidl::Array<EntryPtr>::New(0);

  while (it->Valid() &&
         convert::ExtendedStringView((*it)->key).substr(0, key_prefix.size()) ==
             convert::ExtendedStringView(key_prefix)) {
    EntryPtr entry = Entry::New();
    entry->key = convert::ToArray((*it)->key);
    entries.push_back(std::move(entry));

    page_storage_->GetObject((*it)->object_id, waiter->NewCallback());
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
          callback(Status::IO_ERROR, nullptr, nullptr);
          return;
        }

        for (size_t i = 0; i < results.size(); i++) {
          ftl::StringView object_contents;

          storage::Status read_status = results[i]->GetData(&object_contents);
          if (read_status != storage::Status::OK) {
            callback(Status::IO_ERROR, nullptr, nullptr);
            return;
          }

          entry_list[i]->value = convert::ToArray(object_contents);
        }
        callback(Status::OK, std::move(entry_list), nullptr);
      });
  waiter->Finalize(result_callback);
}

void PageSnapshotImpl::GetKeys(fidl::Array<uint8_t> key_prefix,
                               fidl::Array<uint8_t> token,
                               const GetKeysCallback& callback) {
  std::unique_ptr<storage::Iterator<const storage::Entry>> it =
      contents_->find(key_prefix);
  fidl::Array<fidl::Array<uint8_t>> keys =
      fidl::Array<fidl::Array<uint8_t>>::New(0);

  while (it->Valid() &&
         convert::ExtendedStringView((*it)->key).substr(0, key_prefix.size()) ==
             convert::ExtendedStringView(key_prefix)) {
    keys.push_back(convert::ToArray((*it)->key));
    it->Next();
  }
  callback(Status::OK, std::move(keys), nullptr);
}

void PageSnapshotImpl::Get(fidl::Array<uint8_t> key,
                           const GetCallback& callback) {
  std::unique_ptr<storage::Iterator<const storage::Entry>> it =
      contents_->find(key);
  if (!it->Valid() ||
      convert::ExtendedStringView((*it)->key) !=
          convert::ExtendedStringView(key)) {
    callback(Status::KEY_NOT_FOUND, nullptr);
    return;
  }
  PageUtils::GetReferenceAsValuePtr(page_storage_, (*it)->object_id, callback);
}

void PageSnapshotImpl::GetPartial(fidl::Array<uint8_t> key,
                                  int64_t offset,
                                  int64_t max_size,
                                  const GetPartialCallback& callback) {
  std::unique_ptr<storage::Iterator<const storage::Entry>> it =
      contents_->find(key);
  if (!it->Valid() ||
      convert::ExtendedStringView((*it)->key) !=
          convert::ExtendedStringView(key)) {
    callback(Status::KEY_NOT_FOUND, mx::vmo());
    return;
  }
  PageUtils::GetPartialReferenceAsBuffer(page_storage_, (*it)->object_id,
                                         offset, max_size, callback);
}

}  // namespace ledger
