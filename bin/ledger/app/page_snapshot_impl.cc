// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <functional>
#include <limits>
#include <queue>
#include <vector>

#include "peridot/bin/ledger/app/page_snapshot_impl.h"

#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/memory/ref_counted.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/tasks/task_runner.h"
#include "peridot/bin/ledger/app/fidl/serialization_size.h"
#include "peridot/bin/ledger/app/page_utils.h"
#include "peridot/bin/ledger/callback/trace_callback.h"
#include "peridot/bin/ledger/callback/waiter.h"
#include "peridot/bin/ledger/convert/convert.h"

namespace ledger {
namespace {

template <typename EntryType>
fidl::StructPtr<EntryType> CreateEntry(const storage::Entry& entry) {
  fidl::StructPtr<EntryType> entry_ptr = EntryType::New();
  entry_ptr->key = convert::ToArray(entry.key);
  entry_ptr->priority = entry.priority == storage::KeyPriority::EAGER
                            ? Priority::EAGER
                            : Priority::LAZY;
  return entry_ptr;
}

// Returns the number of handles used by an entry of the given type. Specialized
// for each entry type.
template <class EntryType>
size_t HandleUsed();

template <>
size_t HandleUsed<Entry>() {
  return 1;
}

template <>
size_t HandleUsed<InlinedEntry>() {
  return 0;
}

// Computes the size of an Entry.
size_t ComputeEntrySize(const EntryPtr& entry) {
  return fidl_serialization::GetEntrySize(entry->key.size());
}

// Computes the size of an InlinedEntry.
size_t ComputeEntrySize(const InlinedEntryPtr& entry) {
  return fidl_serialization::GetInlinedEntrySize(entry);
}

// Fills an Entry from the content of object.
storage::Status FillSingleEntry(const storage::Object& object,
                                EntryPtr* entry) {
  return object.GetVmo(&(*entry)->value);
}

// Fills an InlinedEntry from the content of object.
storage::Status FillSingleEntry(const storage::Object& object,
                                InlinedEntryPtr* entry) {
  fxl::StringView data;
  storage::Status status = object.GetData(&data);
  (*entry)->value = convert::ToArray(data);
  return status;
}

// Calls |callback| with filled entries of the provided type per
// GetEntries/GetEntriesInline semantics.
// |fill_value| is a callback that fills the entry pointer with the content of
// the provided object.
template <typename EntryType>
void FillEntries(
    storage::PageStorage* page_storage,
    const std::string& key_prefix,
    const storage::Commit* commit,
    fidl::Array<uint8_t> key_start,
    fidl::Array<uint8_t> token,
    const std::function<void(Status,
                             fidl::Array<fidl::StructPtr<EntryType>>,
                             fidl::Array<uint8_t>)>& callback) {
  // |token| represents the first key to be returned in the list of entries.
  // Initially, all entries starting from |token| are requested from storage.
  // Iteration stops if either all entries were found, or if the estimated
  // serialization size of entries exceeds the maximum size of a FIDL message
  // (fidl_serialization::kMaxInlineDataSize), or if the number of entries
  // exceeds fidl_serialization::kMaxMessageHandles. If inline entries are
  // requested, then the actual size of the message is computed as the values
  // are added to the entries. This may result in less entries sent than
  // initially planned. In the case when not all entries have been sent,
  // callback will run with a PARTIAL_RESULT status and a token appropriate for
  // resuming the iteration at the right place.

  // Represents information shared between on_next and on_done callbacks.
  struct Context {
    fidl::Array<fidl::StructPtr<EntryType>> entries;
    // The serialization size of all entries.
    size_t size = fidl_serialization::kArrayHeaderSize;
    // The number of handles used.
    size_t handle_count = 0u;
    // If |entries| array size exceeds kMaxInlineDataSize, |next_token| will
    // have the value of the following entry's key.
    fidl::Array<uint8_t> next_token;
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
                          : std::max(key_prefix, convert::ToString(key_start));
  auto on_next = fxl::MakeCopyable([
    page_storage, &key_prefix, context = context.get(), waiter
  ](storage::Entry entry) {
    if (!PageUtils::MatchesPrefix(entry.key, key_prefix)) {
      return false;
    }
    context->size += fidl_serialization::GetEntrySize(entry.key.size());
    context->handle_count += HandleUsed<EntryType>();
    if ((context->size > fidl_serialization::kMaxInlineDataSize ||
         context->handle_count > fidl_serialization::kMaxMessageHandles) &&
        !context->entries.empty()) {
      context->next_token = convert::ToArray(entry.key);
      return false;
    }
    context->entries.push_back(CreateEntry<EntryType>(entry));
    page_storage->GetObject(
        entry.object_digest, storage::PageStorage::Location::LOCAL,
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

  auto on_done = fxl::MakeCopyable([
    waiter, context = std::move(context), callback = std::move(timed_callback)
  ](storage::Status status) mutable {
    if (status != storage::Status::OK) {
      FXL_LOG(ERROR) << "Error while reading.";
      callback(Status::IO_ERROR, nullptr, nullptr);
      return;
    }
    std::function<void(storage::Status,
                       std::vector<std::unique_ptr<const storage::Object>>)>
        result_callback = fxl::MakeCopyable([
          callback = std::move(callback), context = std::move(context)
        ](storage::Status status,
          std::vector<std::unique_ptr<const storage::Object>> results) mutable {
          if (status != storage::Status::OK) {
            FXL_LOG(ERROR) << "Error while reading.";
            callback(Status::IO_ERROR, nullptr, nullptr);
            return;
          }
          FXL_DCHECK(context->entries.size() == results.size());
          size_t real_size = 0;
          size_t i = 0;
          for (; i < results.size(); i++) {
            fidl::StructPtr<EntryType>& entry_ptr = context->entries[i];
            if (!results[i]) {
              size_t entry_size = ComputeEntrySize(entry_ptr);
              if (real_size + entry_size >
                  fidl_serialization::kMaxInlineDataSize) {
                break;
              }
              real_size += entry_size;
              // We don't have the object locally, but we decided not to abort.
              // This means this object is a value of a lazy key and the client
              // should ask to retrieve it over the network if they need it.
              // Here, we just leave the value part of the entry null.
              continue;
            }

            storage::Status read_status =
                FillSingleEntry(*results[i], &entry_ptr);
            size_t entry_size = ComputeEntrySize(entry_ptr);
            if (real_size + entry_size >
                fidl_serialization::kMaxInlineDataSize) {
              break;
            }
            real_size += entry_size;
            if (read_status != storage::Status::OK) {
              callback(PageUtils::ConvertStatus(read_status), nullptr, nullptr);
              return;
            }
          }
          if (i != results.size()) {
            if (i == 0) {
              callback(Status::VALUE_TOO_LARGE, nullptr, nullptr);
              return;
            }
            // We had to bail out early because the result would be too big
            // otherwise.
            context->next_token = std::move(context->entries[i]->key);
            context->entries.resize(i);
          }
          if (!context->next_token.empty()) {
            callback(Status::PARTIAL_RESULT, std::move(context->entries),
                     std::move(context->next_token));
            return;
          }
          callback(Status::OK, std::move(context->entries), nullptr);
        });
    waiter->Finalize(result_callback);
  });
  page_storage->GetCommitContents(*commit, std::move(start), std::move(on_next),
                                  std::move(on_done));
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
  FillEntries<Entry>(page_storage_, key_prefix_, commit_.get(),
                     std::move(key_start), std::move(token), callback);
}

void PageSnapshotImpl::GetEntriesInline(
    fidl::Array<uint8_t> key_start,
    fidl::Array<uint8_t> token,
    const GetEntriesInlineCallback& callback) {
  FillEntries<InlinedEntry>(page_storage_, key_prefix_, commit_.get(),
                            std::move(key_start), std::move(token), callback);
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

  auto timed_callback = TRACE_CALLBACK(callback, "ledger", "snapshot_get_keys");

  auto context = std::make_unique<Context>();
  auto on_next = fxl::MakeCopyable(
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
  auto on_done = fxl::MakeCopyable([
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
  auto timed_callback = TRACE_CALLBACK(callback, "ledger", "snapshot_get");

  page_storage_->GetEntryFromCommit(*commit_, convert::ToString(key), [
    this, callback = std::move(timed_callback)
  ](storage::Status status, storage::Entry entry) mutable {
    if (status != storage::Status::OK) {
      callback(PageUtils::ConvertStatus(status, Status::KEY_NOT_FOUND),
               zx::vmo());
      return;
    }
    PageUtils::GetPartialReferenceAsBuffer(
        page_storage_, entry.object_digest, 0u,
        std::numeric_limits<int64_t>::max(),
        storage::PageStorage::Location::LOCAL, Status::NEEDS_FETCH,
        std::move(callback));
  });
}

void PageSnapshotImpl::GetInline(fidl::Array<uint8_t> key,
                                 const GetInlineCallback& callback) {
  auto timed_callback =
      TRACE_CALLBACK(callback, "ledger", "snapshot_get_inline");

  page_storage_->GetEntryFromCommit(*commit_, convert::ToString(key), [
    this, callback = std::move(timed_callback)
  ](storage::Status status, storage::Entry entry) mutable {
    if (status != storage::Status::OK) {
      callback(PageUtils::ConvertStatus(status, Status::KEY_NOT_FOUND),
               nullptr);
      return;
    }
    PageUtils::GetReferenceAsStringView(
        page_storage_, entry.object_digest,
        storage::PageStorage::Location::LOCAL, Status::NEEDS_FETCH,
        [callback = std::move(callback)](Status status,
                                         fxl::StringView data_view) {
          if (fidl_serialization::GetByteArraySize(data_view.size()) +
                  // Size of the Status.
                  fidl_serialization::kEnumSize >
              fidl_serialization::kMaxInlineDataSize) {
            callback(Status::VALUE_TOO_LARGE, nullptr);
            return;
          }
          callback(status, convert::ToArray(data_view));
        });
  });
}

void PageSnapshotImpl::Fetch(fidl::Array<uint8_t> key,
                             const FetchCallback& callback) {
  auto timed_callback = TRACE_CALLBACK(callback, "ledger", "snapshot_fetch");

  page_storage_->GetEntryFromCommit(*commit_, convert::ToString(key), [
    this, callback = std::move(timed_callback)
  ](storage::Status status, storage::Entry entry) {
    if (status != storage::Status::OK) {
      callback(PageUtils::ConvertStatus(status, Status::KEY_NOT_FOUND),
               zx::vmo());
      return;
    }
    PageUtils::GetPartialReferenceAsBuffer(
        page_storage_, entry.object_digest, 0u,
        std::numeric_limits<int64_t>::max(),
        storage::PageStorage::Location::NETWORK, Status::INTERNAL_ERROR,
        callback);
  });
}

void PageSnapshotImpl::FetchPartial(fidl::Array<uint8_t> key,
                                    int64_t offset,
                                    int64_t max_size,
                                    const FetchPartialCallback& callback) {
  auto timed_callback =
      TRACE_CALLBACK(callback, "ledger", "snapshot_fetch_partial");

  page_storage_->GetEntryFromCommit(*commit_, convert::ToString(key), [
    this, offset, max_size, callback = std::move(timed_callback)
  ](storage::Status status, storage::Entry entry) {
    if (status != storage::Status::OK) {
      callback(PageUtils::ConvertStatus(status, Status::KEY_NOT_FOUND),
               zx::vmo());
      return;
    }

    PageUtils::GetPartialReferenceAsBuffer(
        page_storage_, entry.object_digest, offset, max_size,
        storage::PageStorage::Location::NETWORK, Status::INTERNAL_ERROR,
        callback);
  });
}

}  // namespace ledger
