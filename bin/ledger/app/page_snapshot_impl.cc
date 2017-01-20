// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>
#include <queue>
#include <vector>

#include "apps/ledger/src/app/page_snapshot_impl.h"

#include "apps/ledger/src/app/constants.h"
#include "apps/ledger/src/app/page_utils.h"
#include "apps/ledger/src/callback/trace_callback.h"
#include "apps/ledger/src/callback/waiter.h"
#include "apps/ledger/src/convert/convert.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/memory/ref_counted.h"
#include "lib/ftl/memory/ref_ptr.h"
#include "lib/ftl/tasks/task_runner.h"

namespace ledger {
PageSnapshotImpl::PageSnapshotImpl(
    storage::PageStorage* page_storage,
    std::unique_ptr<const storage::Commit> commit)
    : page_storage_(page_storage), commit_(std::move(commit)) {}

PageSnapshotImpl::~PageSnapshotImpl() {}

void PageSnapshotImpl::GetEntries(fidl::Array<uint8_t> key_prefix,
                                  fidl::Array<uint8_t> token,
                                  const GetEntriesCallback& callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "snapshot", "get_entries");

  auto waiter = callback::
      Waiter<storage::Status, std::unique_ptr<const storage::Object>>::Create(
          storage::Status::OK);
  auto entries = std::make_unique<fidl::Array<EntryPtr>>();
  std::string prefix =
      key_prefix.is_null() ? "" : convert::ToString(key_prefix);
  auto on_next = ftl::MakeCopyable(
      [ this, prefix, entries = entries.get(), waiter ](storage::Entry entry) {
        if (convert::ExtendedStringView(entry.key).substr(0, prefix.size()) !=
            convert::ExtendedStringView(prefix)) {
          return false;
        }
        EntryPtr entry_ptr = Entry::New();
        entry_ptr->key = convert::ToArray(entry.key);
        entries->push_back(std::move(entry_ptr));

        page_storage_->GetObject(entry.object_id, waiter->NewCallback());
        return true;
      });

  auto on_done = ftl::MakeCopyable([
    this, waiter, entries = std::move(entries),
    callback = std::move(timed_callback)
  ](storage::Status status) mutable {
    if (status != storage::Status::OK) {
      FTL_LOG(ERROR) << "PageSnapshotImpl::GetEntries error while reading.";
      callback(Status::IO_ERROR, nullptr, nullptr);
      return;
    }
    std::function<void(storage::Status,
                       std::vector<std::unique_ptr<const storage::Object>>)>
        result_callback = ftl::MakeCopyable([
          callback = std::move(callback), entries = std::move(entries)
        ](storage::Status status,
          std::vector<std::unique_ptr<const storage::Object>> results) mutable {
          if (status != storage::Status::OK) {
            FTL_LOG(ERROR)
                << "PageSnapshotImpl::GetEntries error while reading.";
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

            (*entries)[i]->value = Value::New();
            (*entries)[i]->value->set_bytes(convert::ToArray(object_contents));
          }
          callback(Status::OK, std::move(*entries), nullptr);
        });
    waiter->Finalize(result_callback);
  });

  page_storage_->GetCommitContents(*commit_, prefix, std::move(on_next),
                                   std::move(on_done));
}

void PageSnapshotImpl::GetKeys(fidl::Array<uint8_t> key_prefix,
                               fidl::Array<uint8_t> token,
                               const GetKeysCallback& callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "snapshot", "get_keys");

  auto keys = std::make_unique<fidl::Array<fidl::Array<uint8_t>>>();
  auto on_next = ftl::MakeCopyable([
    key_prefix = convert::ToString(key_prefix), keys = keys.get()
  ](storage::Entry entry) {
    if (convert::ExtendedStringView(entry.key).substr(0, key_prefix.size()) !=
        convert::ExtendedStringView(key_prefix)) {
      return false;
    }
    keys->push_back(convert::ToArray(entry.key));
    return true;
  });
  auto on_done = ftl::MakeCopyable([
    keys = std::move(keys), callback = std::move(timed_callback)
  ](storage::Status s) { callback(Status::OK, std::move(*keys), nullptr); });
  page_storage_->GetCommitContents(*commit_, convert::ToString(key_prefix),
                                   std::move(on_next), std::move(on_done));
}

void PageSnapshotImpl::Get(fidl::Array<uint8_t> key,
                           const GetCallback& callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "snapshot", "get");

  page_storage_->GetEntryFromCommit(*commit_, convert::ToString(key), [
    this, callback = std::move(timed_callback)
  ](storage::Status status, storage::Entry entry) {
    if (status != storage::Status::OK) {
      callback(PageUtils::ConvertStatus(status, Status::KEY_NOT_FOUND),
               nullptr);
      return;
    }
    PageUtils::GetReferenceAsValuePtr(page_storage_, entry.object_id,
                                      std::move(callback));
  });
}

void PageSnapshotImpl::GetPartial(fidl::Array<uint8_t> key,
                                  int64_t offset,
                                  int64_t max_size,
                                  const GetPartialCallback& callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "snapshot", "get_partial");

  page_storage_->GetEntryFromCommit(*commit_, convert::ToString(key), [
    this, offset, max_size, callback = std::move(timed_callback)
  ](storage::Status status, storage::Entry entry) {
    if (status != storage::Status::OK) {
      callback(PageUtils::ConvertStatus(status, Status::KEY_NOT_FOUND),
               mx::vmo());
      return;
    }

    PageUtils::GetPartialReferenceAsBuffer(page_storage_, entry.object_id,
                                           offset, max_size, callback);
  });
}

}  // namespace ledger
