// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_snapshot_impl.h"

#include <lib/fidl/cpp/optional.h>
#include <lib/fit/function.h>

#include <algorithm>
#include <functional>
#include <limits>
#include <queue>
#include <vector>

#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/fidl/serialization_size.h"
#include "src/ledger/bin/app/page_utils.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/lib/callback/trace_callback.h"
#include "src/ledger/lib/callback/waiter.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/ledger/lib/memory/ref_counted.h"
#include "src/ledger/lib/memory/ref_ptr.h"
#include "src/ledger/lib/vmo/strings.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {
namespace {

template <typename EntryType>
EntryType CreateEntry(const storage::Entry& entry) {
  EntryType result;
  result.key = convert::ToArray(entry.key);
  result.priority =
      entry.priority == storage::KeyPriority::EAGER ? Priority::EAGER : Priority::LAZY;
  return result;
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
size_t ComputeEntrySize(const Entry& entry) {
  return fidl_serialization::GetEntrySize(entry.key.size());
}

// Computes the size of an InlinedEntry.
size_t ComputeEntrySize(const InlinedEntry& entry) {
  return fidl_serialization::GetInlinedEntrySize(entry);
}

// Fills an Entry from the content of object.
Status FillSingleEntry(const storage::Object& object, Entry* entry) {
  SizedVmo vmo;
  RETURN_ON_ERROR(object.GetVmo(&vmo));
  entry->value = fidl::MakeOptional(std::move(vmo).ToTransport());
  return Status::OK;
}

// Fills an InlinedEntry from the content of object.
Status FillSingleEntry(const storage::Object& object, InlinedEntry* entry) {
  absl::string_view data;
  RETURN_ON_ERROR(object.GetData(&data));
  entry->inlined_value = std::make_unique<InlinedValue>();
  entry->inlined_value->value = convert::ToArray(data);
  return Status::OK;
}

// Calls |callback| with filled entries of the provided type per
// GetEntries/GetEntriesInline semantics.
// |fill_value| is a callback that fills the entry pointer with the content of
// the provided object.
template <typename EntryType>
void FillEntries(
    storage::PageStorage* page_storage, const std::string& key_prefix,
    const storage::Commit* commit, std::vector<uint8_t> key_start, std::unique_ptr<Token> token,
    fit::function<void(Status, std::vector<EntryType>, std::unique_ptr<Token>)> callback) {
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
    std::vector<EntryType> entries;
    // The serialization size of all entries.
    size_t size = fidl_serialization::kVectorHeaderSize;
    // The number of handles used.
    size_t handle_count = 0u;
    // If |entries| array size exceeds kMaxInlineDataSize, |next_token| will
    // have the value of the following entry's key.
    std::unique_ptr<Token> next_token;
  };
  auto timed_callback = TRACE_CALLBACK(std::move(callback), "ledger", "snapshot_get_entries");

  auto waiter = MakeRefCounted<Waiter<Status, std::unique_ptr<const storage::Object>>>(Status::OK);

  auto context = std::make_unique<Context>();
  // Use |token| for the first key if present.
  std::string start = token ? convert::ToString(token->opaque_id)
                            : std::max(key_prefix, convert::ToString(key_start));
  auto on_next = [page_storage, &key_prefix, context = context.get(),
                  waiter](storage::Entry entry) {
    if (!PageUtils::MatchesPrefix(entry.key, key_prefix)) {
      return false;
    }
    context->size += fidl_serialization::GetEntrySize(entry.key.size());
    context->handle_count += HandleUsed<EntryType>();
    if ((context->size > fidl_serialization::kMaxInlineDataSize ||
         context->handle_count > fidl_serialization::kMaxMessageHandles) &&
        !context->entries.empty()) {
      context->next_token = std::make_unique<Token>();
      context->next_token->opaque_id = convert::ToArray(entry.key);
      return false;
    }
    context->entries.push_back(CreateEntry<EntryType>(entry));
    page_storage->GetObject(
        entry.object_identifier, storage::PageStorage::Location::Local(),
        [priority = entry.priority, waiter_callback = waiter->NewCallback()](
            Status status, std::unique_ptr<const storage::Object> object) {
          if (status == Status::INTERNAL_NOT_FOUND && priority == storage::KeyPriority::LAZY) {
            waiter_callback(Status::OK, nullptr);
          } else {
            waiter_callback(status, std::move(object));
          }
        });
    return true;
  };

  auto on_done = [waiter, context = std::move(context),
                  callback = std::move(timed_callback)](Status status) mutable {
    if (status != Status::OK) {
      LEDGER_LOG(ERROR) << "Error while reading: " << status;
      callback(Status::IO_ERROR, std::vector<EntryType>(), nullptr);
      return;
    }
    fit::function<void(Status, std::vector<std::unique_ptr<const storage::Object>>)>
        result_callback = [callback = std::move(callback), context = std::move(context)](
                              Status status,
                              std::vector<std::unique_ptr<const storage::Object>> results) mutable {
          if (status != Status::OK) {
            LEDGER_LOG(ERROR) << "Error while reading: " << status;
            callback(Status::IO_ERROR, std::vector<EntryType>(), nullptr);
            return;
          }
          LEDGER_DCHECK(context->entries.size() == results.size());
          size_t real_size = 0;
          size_t i = 0;
          for (; i < results.size(); i++) {
            EntryType& entry = context->entries.at(i);
            size_t next_token_size =
                i + 1 >= results.size()
                    ? 0
                    : fidl_serialization::GetByteVectorSize(context->entries.at(i + 1).key.size());
            if (!results[i]) {
              size_t entry_size = ComputeEntrySize(entry);
              if (real_size + entry_size + next_token_size >
                  fidl_serialization::kMaxInlineDataSize) {
                break;
              }
              real_size += entry_size;
              // We don't have the object locally, but we decided not to
              // abort. This means this object is a value of a lazy key
              // and the client should ask to retrieve it over the
              // network if they need it. Here, we just leave the value
              // part of the entry null.
              continue;
            }

            Status read_status = FillSingleEntry(*results[i], &entry);
            if (read_status != Status::OK) {
              callback(read_status, std::vector<EntryType>(), nullptr);
              return;
            }
            size_t entry_size = ComputeEntrySize(entry);
            if (real_size + entry_size + next_token_size > fidl_serialization::kMaxInlineDataSize) {
              break;
            }
            real_size += entry_size;
          }
          if (i != results.size()) {
            if (i == 0) {
              callback(Status::ILLEGAL_STATE, std::vector<EntryType>(), nullptr);
              return;
            }
            // We had to bail out early because the result would be too
            // big otherwise.
            context->next_token = std::make_unique<Token>();
            context->next_token->opaque_id = std::move(context->entries.at(i).key);
            context->entries.resize(i);
          }
          callback(Status::OK, std::move(context->entries), std::move(context->next_token));
        };
    waiter->Finalize(std::move(result_callback));
  };
  page_storage->GetCommitContents(*commit, std::move(start), std::move(on_next),
                                  std::move(on_done));
}

template <typename Result>
Result ToErrorResult(fuchsia::ledger::Error error) {
  Result result;
  result.set_err(error);
  return result;
}
}  // namespace

PageSnapshotImpl::PageSnapshotImpl(storage::PageStorage* page_storage,
                                   std::unique_ptr<const storage::Commit> commit,
                                   std::string key_prefix)
    : page_storage_(page_storage), commit_(std::move(commit)), key_prefix_(std::move(key_prefix)) {}

PageSnapshotImpl::~PageSnapshotImpl() = default;

void PageSnapshotImpl::GetEntries(
    std::vector<uint8_t> key_start, std::unique_ptr<Token> token,
    fit::function<void(Status, std::vector<Entry>, std::unique_ptr<Token>)> callback) {
  FillEntries<Entry>(page_storage_, key_prefix_, commit_.get(), std::move(key_start),
                     std::move(token), std::move(callback));
}

void PageSnapshotImpl::GetEntriesInline(
    std::vector<uint8_t> key_start, std::unique_ptr<Token> token,
    fit::function<void(Status, std::vector<InlinedEntry>, std::unique_ptr<Token>)> callback) {
  FillEntries<InlinedEntry>(page_storage_, key_prefix_, commit_.get(), std::move(key_start),
                            std::move(token), std::move(callback));
}

void PageSnapshotImpl::GetKeys(
    std::vector<uint8_t> key_start, std::unique_ptr<Token> token,
    fit::function<void(Status, std::vector<std::vector<uint8_t>>, std::unique_ptr<Token>)>
        callback) {
  // Represents the information that needs to be shared between on_next and
  // on_done callbacks.
  struct Context {
    // The result of GetKeys. New keys from on_next are appended to this array.
    std::vector<std::vector<uint8_t>> keys;
    // The total size in number of bytes of the |keys| array.
    size_t size = fidl_serialization::kVectorHeaderSize;
    // If the |keys| array size exceeds the maximum allowed inlined data size,
    // |next_token| will have the value of the next key (not included in array)
    // which can be used as the next token.
    std::unique_ptr<Token> next_token;
  };

  auto timed_callback = TRACE_CALLBACK(std::move(callback), "ledger", "snapshot_get_keys");

  auto context = std::make_unique<Context>();
  auto on_next = [this, context = context.get()](storage::Entry entry) {
    if (!PageUtils::MatchesPrefix(entry.key, key_prefix_)) {
      return false;
    }
    context->size += fidl_serialization::GetByteVectorSize(entry.key.size());
    if (context->size > fidl_serialization::kMaxInlineDataSize) {
      context->next_token = std::make_unique<Token>();
      context->next_token->opaque_id = convert::ToArray(entry.key);
      return false;
    }
    context->keys.push_back(convert::ToArray(entry.key));
    return true;
  };
  auto on_done = [context = std::move(context),
                  callback = std::move(timed_callback)](Status status) {
    if (status != Status::OK) {
      LEDGER_LOG(ERROR) << "Error while reading: " << status;
      callback(Status::IO_ERROR, std::vector<std::vector<uint8_t>>(), nullptr);
      return;
    }
    callback(Status::OK, std::move(context->keys), std::move(context->next_token));
  };
  if (token) {
    page_storage_->GetCommitContents(*commit_, convert::ToString(token->opaque_id),
                                     std::move(on_next), std::move(on_done));
  } else {
    page_storage_->GetCommitContents(*commit_, std::max(convert::ToString(key_start), key_prefix_),
                                     std::move(on_next), std::move(on_done));
  }
}

void PageSnapshotImpl::Get(
    std::vector<uint8_t> key,
    fit::function<void(Status, fuchsia::ledger::PageSnapshot_Get_Result)> callback) {
  auto timed_callback = TRACE_CALLBACK(std::move(callback), "ledger", "snapshot_get");

  page_storage_->GetEntryFromCommit(
      *commit_, convert::ToString(key),
      [this, callback = std::move(timed_callback)](Status status, storage::Entry entry) mutable {
        if (status == Status::KEY_NOT_FOUND) {
          callback(Status::OK, ToErrorResult<fuchsia::ledger::PageSnapshot_Get_Result>(
                                   fuchsia::ledger::Error::KEY_NOT_FOUND));
          return;
        }
        if (status != Status::OK) {
          callback(status, fuchsia::ledger::PageSnapshot_Get_Result());
          return;
        }
        page_storage_->GetObjectPart(
            entry.object_identifier, 0u, std::numeric_limits<int64_t>::max(),
            storage::PageStorage::Location::Local(),
            [callback = std::move(callback)](Status status, SizedVmo data) {
              if (status == Status::INTERNAL_NOT_FOUND) {
                callback(Status::OK, ToErrorResult<fuchsia::ledger::PageSnapshot_Get_Result>(
                                         fuchsia::ledger::Error::NEEDS_FETCH));
                return;
              }
              if (status != Status::OK) {
                callback(status, fuchsia::ledger::PageSnapshot_Get_Result());
                return;
              }
              fuchsia::ledger::PageSnapshot_Get_Result result;
              result.response().buffer = std::move(data).ToTransport();
              callback(Status::OK, std::move(result));
            });
      });
}

void PageSnapshotImpl::GetInline(
    std::vector<uint8_t> key,
    fit::function<void(Status, fuchsia::ledger::PageSnapshot_GetInline_Result)> callback) {
  auto timed_callback = TRACE_CALLBACK(std::move(callback), "ledger", "snapshot_get_inline");

  page_storage_->GetEntryFromCommit(
      *commit_, convert::ToString(key),
      [this, callback = std::move(timed_callback)](Status status, storage::Entry entry) mutable {
        if (status == Status::KEY_NOT_FOUND) {
          callback(Status::OK, ToErrorResult<fuchsia::ledger::PageSnapshot_GetInline_Result>(
                                   fuchsia::ledger::Error::KEY_NOT_FOUND));
          return;
        }
        if (status != Status::OK) {
          callback(status, fuchsia::ledger::PageSnapshot_GetInline_Result());
          return;
        }
        PageUtils::ResolveObjectIdentifierAsStringView(
            page_storage_, entry.object_identifier, storage::PageStorage::Location::Local(),
            [callback = std::move(callback)](Status status, absl::string_view data_view) {
              if (status == Status::INTERNAL_NOT_FOUND) {
                callback(Status::OK, ToErrorResult<fuchsia::ledger::PageSnapshot_GetInline_Result>(
                                         fuchsia::ledger::Error::NEEDS_FETCH));
                return;
              }
              if (status != Status::OK) {
                callback(status, fuchsia::ledger::PageSnapshot_GetInline_Result());
                return;
              }
              if (fidl_serialization::GetByteVectorSize(data_view.size()) +
                      fidl_serialization::kStatusEnumSize >
                  fidl_serialization::kMaxInlineDataSize) {
                callback(Status::ILLEGAL_STATE, fuchsia::ledger::PageSnapshot_GetInline_Result());
                return;
              }
              fuchsia::ledger::PageSnapshot_GetInline_Result result;
              result.response().value.value = convert::ToArray(data_view);
              callback(Status::OK, std::move(result));
            });
      });
}

void PageSnapshotImpl::Fetch(
    std::vector<uint8_t> key,
    fit::function<void(Status, fuchsia::ledger::PageSnapshot_Fetch_Result)> callback) {
  FetchPartial(std::move(key), 0, -1,
               [callback = std::move(callback)](
                   Status status, fuchsia::ledger::PageSnapshot_FetchPartial_Result result) {
                 if (status != Status::OK) {
                   callback(status, fuchsia::ledger::PageSnapshot_Fetch_Result());
                   return;
                 }
                 fuchsia::ledger::PageSnapshot_Fetch_Result new_result;
                 if (result.is_err()) {
                   new_result.set_err(result.err());
                 } else {
                   new_result.response().buffer = std::move(result.response().buffer);
                 }
                 callback(Status::OK, std::move(new_result));
               });
}

void PageSnapshotImpl::FetchPartial(
    std::vector<uint8_t> key, int64_t offset, int64_t max_size,
    fit::function<void(Status, fuchsia::ledger::PageSnapshot_FetchPartial_Result)> callback) {
  auto timed_callback = TRACE_CALLBACK(std::move(callback), "ledger", "snapshot_fetch_partial");

  page_storage_->GetEntryFromCommit(
      *commit_, convert::ToString(key),
      [this, offset, max_size, callback = std::move(timed_callback)](Status status,
                                                                     storage::Entry entry) mutable {
        if (status == Status::KEY_NOT_FOUND) {
          callback(Status::OK, ToErrorResult<fuchsia::ledger::PageSnapshot_FetchPartial_Result>(
                                   fuchsia::ledger::Error::KEY_NOT_FOUND));
          return;
        }
        if (status != Status::OK) {
          callback(status, fuchsia::ledger::PageSnapshot_FetchPartial_Result());
          return;
        }

        page_storage_->GetObjectPart(
            entry.object_identifier, offset, max_size,
            storage::PageStorage::Location::ValueFromNetwork(),
            [callback = std::move(callback)](Status status, SizedVmo data) {
              if (status == Status::NETWORK_ERROR) {
                callback(Status::OK,
                         ToErrorResult<fuchsia::ledger::PageSnapshot_FetchPartial_Result>(
                             fuchsia::ledger::Error::NETWORK_ERROR));
                return;
              }
              if (status != Status::OK) {
                callback(status, fuchsia::ledger::PageSnapshot_FetchPartial_Result());
                return;
              }
              fuchsia::ledger::PageSnapshot_FetchPartial_Result result;
              result.response().buffer = std::move(data).ToTransport();
              callback(Status::OK, std::move(result));
            });
      });
}

}  // namespace ledger
