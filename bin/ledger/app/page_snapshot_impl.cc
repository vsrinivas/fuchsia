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
#include "lib/mtl/vmo/strings.h"

namespace ledger {
namespace {

const size_t kFidlArrayHeaderSize = sizeof(fidl::internal::Array_Data<char>);
const size_t kFidlPointerSize = sizeof(uint64_t);
const size_t kFidlPrioritySize = sizeof(int32_t);
const size_t kFidlHandleSize = sizeof(int32_t);

const size_t kEstimatedValueLength = 16;
const size_t kMaxInlinedValueLength = 256;

EntryPtr CreateEntry(const storage::Entry& entry) {
  EntryPtr entry_ptr = Entry::New();
  entry_ptr->key = convert::ToArray(entry.key);
  entry_ptr->priority = entry.priority == storage::KeyPriority::EAGER
                            ? Priority::EAGER
                            : Priority::LAZY;
  return entry_ptr;
}

bool MatchesPrefix(const std::string& key, const std::string& prefix) {
  return convert::ExtendedStringView(key).substr(0, prefix.size()) ==
         convert::ExtendedStringView(prefix);
}

size_t GetValueFidlSize(size_t value_length, bool value_as_bytes) {
  return value_as_bytes ? value_length + kFidlArrayHeaderSize : kFidlHandleSize;
}

size_t GetEntryFidlSize(size_t key_length,
                        size_t value_length,
                        bool value_as_bytes) {
  size_t key_size = key_length + kFidlArrayHeaderSize;
  size_t object_size = GetValueFidlSize(value_length, value_as_bytes);
  return kFidlPointerSize + key_size + object_size + kFidlPrioritySize;
}

Status UpdateEntryValue(EntryPtr* entry,
                        ftl::StringView object_contents,
                        bool value_as_bytes) {
  (*entry)->value = Value::New();
  if (value_as_bytes) {
    (*entry)->value->set_bytes(convert::ToArray(object_contents));
  } else {
    mx::vmo buffer;
    bool vmo_success = mtl::VmoFromString(object_contents, &buffer);
    if (!vmo_success) {
      FTL_LOG(ERROR) << "Failed to create vmo.";
      return Status::IO_ERROR;
    }
    (*entry)->value->set_buffer(std::move(buffer));
  }
  return Status::OK;
}

}  // namespace

PageSnapshotImpl::PageSnapshotImpl(
    storage::PageStorage* page_storage,
    std::unique_ptr<const storage::Commit> commit)
    : page_storage_(page_storage), commit_(std::move(commit)) {}

PageSnapshotImpl::~PageSnapshotImpl() {}

void PageSnapshotImpl::GetEntries(fidl::Array<uint8_t> key_prefix,
                                  fidl::Array<uint8_t> token,
                                  const GetEntriesCallback& callback) {
  // |token| represents the first key to be returned in the list of entries.
  // Initially, all entries starting from |token| are requested from storage.
  // Iteration over the entries stops if either alls were found, or if the
  // serialization size of entries including the value (with an estimated length
  // of kEstimatedValueLength) exceeds kMaxInlineDataSize.
  //
  // Once requested objects are retrieved, the accurate serialization size can
  // be computed:
  //   - If the actual size is bigger than kMaxInlineDataSize, a subarray of
  //   entries is returned.
  //   - Otherwise, the array of entries is returned as a complete (Status::OK)
  //   or a partial result (PARTIAL_RESULT), without trying to fetch any
  //   additional entries.
  //
  // In any case, if the value size exceeds kMaxInlinedValueLength, a VMO is
  // used instead of the value bytes.

  // Represents information shared between on_next and on_done callbacks.
  struct Context {
    fidl::Array<EntryPtr> entries;
    // The estimated serialization size of all entries.
    size_t estimated_size;
    // If |entries| array is estimated to exceed kMaxInlineDataSize,
    // |next_token| will have the value of the following entry's key.
    std::string next_token = "";
  };
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "snapshot", "get_entries");

  auto waiter = callback::
      Waiter<storage::Status, std::unique_ptr<const storage::Object>>::Create(
          storage::Status::OK);

  auto context = std::make_unique<Context>();
  std::string prefix = convert::ToString(key_prefix);
  auto on_next = ftl::MakeCopyable(
      [ this, prefix, context = context.get(), waiter ](storage::Entry entry) {
        if (!MatchesPrefix(entry.key, prefix)) {
          return false;
        }
        context->estimated_size +=
            GetEntryFidlSize(entry.key.size(), kEstimatedValueLength, true);
        if (context->estimated_size > kMaxInlineDataSize) {
          context->next_token = std::move(entry.key);
          return false;
        }
        context->entries.push_back(CreateEntry(entry));
        page_storage_->GetObject(entry.object_id,
                                 storage::PageStorage::Location::LOCAL,
                                 waiter->NewCallback());
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

          size_t size = kFidlArrayHeaderSize;
          for (size_t i = 0; i < results.size(); i++) {
            ftl::StringView object_contents;
            storage::Status read_status = results[i]->GetData(&object_contents);
            if (read_status != storage::Status::OK) {
              callback(Status::IO_ERROR, nullptr, nullptr);
              return;
            }

            EntryPtr& entry_ptr = context->entries[i];
            bool value_as_bytes =
                object_contents.size() <= kMaxInlinedValueLength;

            size += GetEntryFidlSize(entry_ptr->key.size(),
                                     object_contents.size(), value_as_bytes);
            if (size > kMaxInlineDataSize) {
              // Make sure there is at least one element in the result.
              if (i > 0) {
                fidl::Array<uint8_t> next_token = std::move(entry_ptr->key);
                context->entries.resize(i);
                callback(Status::PARTIAL_RESULT, std::move(context->entries),
                         std::move(next_token));
                return;
              }
              // Use a handle<vmo> for the value, instead of the object bytes.
              size_t object_size =
                  GetValueFidlSize(object_contents.size(), value_as_bytes);
              size = size - object_size + kFidlHandleSize;
              value_as_bytes = false;
            }
            Status status =
                UpdateEntryValue(&entry_ptr, object_contents, value_as_bytes);
            if (status != Status::OK) {
              callback(status, nullptr, nullptr);
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
  // Use |prefix| for the stopping condition, but use |token| for the first key.
  if (token) {
    prefix = convert::ToString(token);
  }
  page_storage_->GetCommitContents(*commit_, std::move(prefix),
                                   std::move(on_next), std::move(on_done));
}

void PageSnapshotImpl::GetKeys(fidl::Array<uint8_t> key_prefix,
                               fidl::Array<uint8_t> token,
                               const GetKeysCallback& callback) {
  // Represents the information that needs to be shared between on_next and
  // on_done callbacks.
  struct Context {
    // The result of GetKeys. New keys from on_next are appended to this array.
    fidl::Array<fidl::Array<uint8_t>> keys;
    // The total size in number of bytes of the |keys| array.
    size_t size = 0;
    // If the |keys| array size exceeds the maximum allowed inlined data size,
    // |next_token| will have the value of the next key (not included in array)
    // which can be used as the next token.
    std::string next_token = "";
  };

  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "snapshot", "get_keys");

  auto context = std::make_unique<Context>();
  auto on_next = ftl::MakeCopyable([
    key_prefix = convert::ToString(key_prefix), context = context.get()
  ](storage::Entry entry) {
    if (!MatchesPrefix(entry.key, key_prefix)) {
      return false;
    }
    context->size += entry.key.size() + kFidlArrayHeaderSize;
    if (context->size > kMaxInlineDataSize) {
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
    page_storage_->GetCommitContents(*commit_, convert::ToString(key_prefix),
                                     std::move(on_next), std::move(on_done));

  } else {
    page_storage_->GetCommitContents(*commit_, convert::ToString(token),
                                     std::move(on_next), std::move(on_done));
  }
}

void PageSnapshotImpl::Get(fidl::Array<uint8_t> key,
                           const GetCallback& callback) {
  auto timed_callback = TRACE_CALLBACK(std::move(callback), "snapshot", "get");

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
