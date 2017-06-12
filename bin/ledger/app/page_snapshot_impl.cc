// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <functional>
#include <limits>
#include <queue>
#include <vector>

#include "apps/ledger/src/app/page_snapshot_impl.h"

#include "apps/ledger/src/app/fidl/serialization_size.h"
#include "apps/ledger/src/app/page_utils.h"
#include "apps/ledger/src/callback/trace_callback.h"
#include "apps/ledger/src/callback/waiter.h"
#include "apps/ledger/src/convert/convert.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/memory/ref_counted.h"
#include "lib/ftl/memory/ref_ptr.h"
#include "lib/ftl/tasks/task_runner.h"
#include "lib/mtl/vmo/strings.h"

namespace ledger {
namespace {

EntryPtr CreateEntry(const storage::Entry& entry) {
  EntryPtr entry_ptr = Entry::New();
  entry_ptr->key = convert::ToArray(entry.key);
  entry_ptr->priority = entry.priority == storage::KeyPriority::EAGER
                            ? Priority::EAGER
                            : Priority::LAZY;
  return entry_ptr;
}

}  // namespace

PageSnapshotImpl::PageSnapshotImpl(
    storage::PageStorage* page_storage,
    std::unique_ptr<const storage::Commit> commit,
    std::string key_prefix)
    : page_storage_(page_storage),
      commit_(std::move(commit)),
      key_prefix_(std::move(key_prefix)) {}

PageSnapshotImpl::~PageSnapshotImpl() {}

void PageSnapshotImpl::GetEntries(fidl::Array<uint8_t> key_start,
                                  fidl::Array<uint8_t> token,
                                  const GetEntriesCallback& callback) {
  // |token| represents the first key to be returned in the list of entries.
  // Initially, all entries starting from |token| are requested from storage.
  // Iteration stops if either all entries were found, or if the serialization
  // size of entries, including the value, exceeds
  // fidl_serialization::kMaxInlineDataSize, or if the number of entries exceeds
  // fidl_serialization::kMaxMessageHandles. In the second and third case
  // callback will run with PARTIAL_RESULT status.

  // Represents information shared between on_next and on_done callbacks.
  struct Context {
    fidl::Array<EntryPtr> entries;
    // The serialization size of all entries.
    size_t size = fidl_serialization::kArrayHeaderSize;
    // The number of entries.
    size_t entry_count = 0u;
    // If |entries| array size exceeds kMaxInlineDataSize, |next_token| will
    // have the value of the following entry's key.
    std::string next_token = "";
  };
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "snapshot_get_entries");

  auto waiter = callback::
      Waiter<storage::Status, std::unique_ptr<const storage::Object>>::Create(
          storage::Status::OK);

  auto context = std::make_unique<Context>();
  // Use |token| for the first key if present.
  std::string start = token
                          ? convert::ToString(token)
                          : std::max(key_prefix_, convert::ToString(key_start));
  auto on_next = ftl::MakeCopyable([ this, context = context.get(),
                                     waiter ](storage::Entry entry) {
    if (!PageUtils::MatchesPrefix(entry.key, key_prefix_)) {
      return false;
    }
    context->size += fidl_serialization::GetEntrySize(entry.key.size());
    ++context->entry_count;
    if ((context->size > fidl_serialization::kMaxInlineDataSize ||
         context->entry_count > fidl_serialization::kMaxMessageHandles) &&
        context->entries.size()) {
      context->next_token = std::move(entry.key);
      return false;
    }
    context->entries.push_back(CreateEntry(entry));
    page_storage_->GetObject(
        entry.object_id, storage::PageStorage::Location::LOCAL,
        [ priority = entry.priority, waiter_callback = waiter->NewCallback() ](
            storage::Status status,
            std::unique_ptr<const storage::Object> object) {
          if (status == storage::Status::NOT_FOUND &&
              priority == storage::KeyPriority::LAZY) {
            waiter_callback(storage::Status::OK, nullptr);
          } else {
            waiter_callback(status, std::move(object));
          }
        });
    return true;
  });

  auto on_done = ftl::MakeCopyable([
    waiter, context = std::move(context), callback = std::move(timed_callback)
  ](storage::Status status) mutable {
    if (status != storage::Status::OK) {
      FTL_LOG(ERROR) << "Error while reading.";
      callback(Status::IO_ERROR, nullptr, nullptr);
      return;
    }
    std::function<void(storage::Status,
                       std::vector<std::unique_ptr<const storage::Object>>)>
        result_callback = ftl::MakeCopyable([
          callback = std::move(callback), context = std::move(context)
        ](storage::Status status,
          std::vector<std::unique_ptr<const storage::Object>> results) mutable {
          if (status != storage::Status::OK) {
            FTL_LOG(ERROR) << "Error while reading.";
            callback(Status::IO_ERROR, nullptr, nullptr);
            return;
          }
          FTL_DCHECK(context->entries.size() == results.size());

          for (size_t i = 0; i < results.size(); i++) {
            if (!results[i]) {
              // We don't have the object locally, but we decided not to abort.
              // This means this object is a value of a lazy key and the client
              // should ask to retrieve it over the network if they need it.
              // Here, we just leave the value part of the entry null.
              continue;
            }

            EntryPtr& entry_ptr = context->entries[i];
            storage::Status read_status = results[i]->GetVmo(&entry_ptr->value);
            if (read_status != storage::Status::OK) {
              callback(
                  PageUtils::ConvertStatus(read_status, Status::INTERNAL_ERROR),
                  nullptr, nullptr);
              return;
            }
          }
          if (!context->next_token.empty()) {
            callback(Status::PARTIAL_RESULT, std::move(context->entries),
                     convert::ToArray(context->next_token));
            return;
          }
          callback(Status::OK, std::move(context->entries), nullptr);
        });
    waiter->Finalize(result_callback);
  });
  page_storage_->GetCommitContents(*commit_, std::move(start),
                                   std::move(on_next), std::move(on_done));
}

void PageSnapshotImpl::GetKeys(fidl::Array<uint8_t> key_start,
                               fidl::Array<uint8_t> token,
                               const GetKeysCallback& callback) {
  // Represents the information that needs to be shared between on_next and
  // on_done callbacks.
  struct Context {
    // The result of GetKeys. New keys from on_next are appended to this array.
    fidl::Array<fidl::Array<uint8_t>> keys;
    // The total size in number of bytes of the |keys| array.
    size_t size = fidl_serialization::kArrayHeaderSize;
    // If the |keys| array size exceeds the maximum allowed inlined data size,
    // |next_token| will have the value of the next key (not included in array)
    // which can be used as the next token.
    std::string next_token = "";
  };

  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "snapshot_get_keys");

  auto context = std::make_unique<Context>();
  auto on_next = ftl::MakeCopyable(
      [ this, context = context.get() ](storage::Entry entry) {
        if (!PageUtils::MatchesPrefix(entry.key, key_prefix_)) {
          return false;
        }
        context->size += fidl_serialization::GetByteArraySize(entry.key.size());
        if (context->size > fidl_serialization::kMaxInlineDataSize) {
          context->next_token = entry.key;
          return false;
        }
        context->keys.push_back(convert::ToArray(entry.key));
        return true;
      });
  auto on_done = ftl::MakeCopyable([
    context = std::move(context), callback = std::move(timed_callback)
  ](storage::Status s) {
    if (context->next_token.empty()) {
      callback(Status::OK, std::move(context->keys), nullptr);
    } else {
      callback(Status::PARTIAL_RESULT, std::move(context->keys),
               convert::ToArray(context->next_token));
    }
  });
  if (token.is_null()) {
    page_storage_->GetCommitContents(
        *commit_, std::max(convert::ToString(key_start), key_prefix_),
        std::move(on_next), std::move(on_done));

  } else {
    page_storage_->GetCommitContents(*commit_, convert::ToString(token),
                                     std::move(on_next), std::move(on_done));
  }
}

void PageSnapshotImpl::Get(fidl::Array<uint8_t> key,
                           const GetCallback& callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "snapshot_get");

  page_storage_->GetEntryFromCommit(*commit_, convert::ToString(key), [
    this, callback = std::move(timed_callback)
  ](storage::Status status, storage::Entry entry) {
    if (status != storage::Status::OK) {
      callback(PageUtils::ConvertStatus(status, Status::KEY_NOT_FOUND),
               mx::vmo());
      return;
    }
    PageUtils::GetPartialReferenceAsBuffer(
        page_storage_, entry.object_id, 0u, std::numeric_limits<int64_t>::max(),
        storage::PageStorage::Location::LOCAL, Status::NEEDS_FETCH,
        std::move(callback));
  });
}

void PageSnapshotImpl::Fetch(fidl::Array<uint8_t> key,
                             const FetchCallback& callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "snapshot_fetch");

  page_storage_->GetEntryFromCommit(*commit_, convert::ToString(key), [
    this, callback = std::move(timed_callback)
  ](storage::Status status, storage::Entry entry) {
    if (status != storage::Status::OK) {
      callback(PageUtils::ConvertStatus(status, Status::KEY_NOT_FOUND),
               mx::vmo());
      return;
    }
    PageUtils::GetPartialReferenceAsBuffer(
        page_storage_, entry.object_id, 0u, std::numeric_limits<int64_t>::max(),
        storage::PageStorage::Location::NETWORK, Status::INTERNAL_ERROR,
        std::move(callback));
  });
}

void PageSnapshotImpl::FetchPartial(fidl::Array<uint8_t> key,
                                    int64_t offset,
                                    int64_t max_size,
                                    const FetchPartialCallback& callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "snapshot_fetch_partial");

  page_storage_->GetEntryFromCommit(*commit_, convert::ToString(key), [
    this, offset, max_size, callback = std::move(timed_callback)
  ](storage::Status status, storage::Entry entry) {
    if (status != storage::Status::OK) {
      callback(PageUtils::ConvertStatus(status, Status::KEY_NOT_FOUND),
               mx::vmo());
      return;
    }

    PageUtils::GetPartialReferenceAsBuffer(
        page_storage_, entry.object_id, offset, max_size,
        storage::PageStorage::Location::NETWORK, Status::INTERNAL_ERROR,
        callback);
  });
}

}  // namespace ledger
