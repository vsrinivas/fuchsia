// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/page_snapshot_impl.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <queue>
#include <vector>

#include <lib/fit/function.h>

#include "lib/callback/trace_callback.h"
#include "lib/callback/waiter.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/memory/ref_counted.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "peridot/bin/ledger/app/constants.h"
#include "peridot/bin/ledger/app/fidl/serialization_size.h"
#include "peridot/bin/ledger/app/page_utils.h"
#include "peridot/lib/convert/convert.h"

namespace ledger {
namespace {

// Transform a SizedVmo to an optional Buffer. Returns null when
// status is not OK, or a not-null transport otherwise.
fuchsia::mem::BufferPtr ToOptionalTransport(Status status, fsl::SizedVmo vmo) {
  if (status != Status::OK) {
    return nullptr;
  }
  return fidl::MakeOptional(std::move(vmo).ToTransport());
}

template <typename EntryType>
EntryType CreateEntry(const storage::Entry& entry) {
  EntryType result;
  result.key = convert::ToArray(entry.key);
  result.priority = entry.priority == storage::KeyPriority::EAGER
                        ? Priority::EAGER
                        : Priority::LAZY;
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
  return fidl_serialization::GetEntrySize(entry.key->size());
}

// Computes the size of an InlinedEntry.
size_t ComputeEntrySize(const InlinedEntry& entry) {
  return fidl_serialization::GetInlinedEntrySize(entry);
}

// Fills an Entry from the content of object.
storage::Status FillSingleEntry(const storage::Object& object, Entry* entry) {
  fsl::SizedVmo vmo;
  storage::Status status = object.GetVmo(&vmo);
  if (status != storage::Status::OK) {
    return status;
  }
  entry->value = fidl::MakeOptional(std::move(vmo).ToTransport());
  return storage::Status::OK;
}

// Fills an InlinedEntry from the content of object.
storage::Status FillSingleEntry(const storage::Object& object,
                                InlinedEntry* entry) {
  fxl::StringView data;
  storage::Status status = object.GetData(&data);
  if (status != storage::Status::OK) {
    return status;
  }
  entry->inlined_value = std::make_unique<InlinedValue>();
  entry->inlined_value->value = convert::ToArray(data);
  return storage::Status::OK;
}

// Calls |callback| with filled entries of the provided type per
// GetEntries/GetEntriesInline semantics.
// |fill_value| is a callback that fills the entry pointer with the content of
// the provided object.
template <typename EntryType>
void FillEntries(storage::PageStorage* page_storage,
                 const std::string& key_prefix, const storage::Commit* commit,
                 fidl::VectorPtr<uint8_t> key_start,
                 std::unique_ptr<Token> token,
                 fit::function<void(Status, fidl::VectorPtr<EntryType>,
                                    std::unique_ptr<Token>)>
                     callback) {
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
    fidl::VectorPtr<EntryType> entries = fidl::VectorPtr<EntryType>::New(0);
    // The serialization size of all entries.
    size_t size = fidl_serialization::kVectorHeaderSize;
    // The number of handles used.
    size_t handle_count = 0u;
    // If |entries| array size exceeds kMaxInlineDataSize, |next_token| will
    // have the value of the following entry's key.
    std::unique_ptr<Token> next_token;
  };
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "snapshot_get_entries");

  auto waiter = fxl::MakeRefCounted<callback::Waiter<
      storage::Status, std::unique_ptr<const storage::Object>>>(
      storage::Status::OK);

  auto context = std::make_unique<Context>();
  // Use |token| for the first key if present.
  std::string start = token
                          ? convert::ToString(token->opaque_id)
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
        !context->entries->empty()) {
      context->next_token = std::make_unique<Token>();
      context->next_token->opaque_id = convert::ToArray(entry.key);
      return false;
    }
    context->entries.push_back(CreateEntry<EntryType>(entry));
    page_storage->GetObject(
        entry.object_identifier, storage::PageStorage::Location::LOCAL,
        [priority = entry.priority, waiter_callback = waiter->NewCallback()](
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
  };

  auto on_done = [waiter, context = std::move(context),
                  callback = std::move(timed_callback)](
                     storage::Status status) mutable {
    if (status != storage::Status::OK) {
      FXL_LOG(ERROR) << "Error while reading: " << status;
      callback(Status::IO_ERROR, fidl::VectorPtr<EntryType>::New(0), nullptr);
      return;
    }
    fit::function<void(storage::Status,
                       std::vector<std::unique_ptr<const storage::Object>>)>
        result_callback =
            [callback = std::move(callback), context = std::move(context)](
                storage::Status status,
                std::vector<std::unique_ptr<const storage::Object>>
                    results) mutable {
              if (status != storage::Status::OK) {
                FXL_LOG(ERROR) << "Error while reading: " << status;
                callback(Status::IO_ERROR, fidl::VectorPtr<EntryType>::New(0),
                         nullptr);
                return;
              }
              FXL_DCHECK(context->entries->size() == results.size());
              size_t real_size = 0;
              size_t i = 0;
              for (; i < results.size(); i++) {
                EntryType& entry = context->entries->at(i);
                size_t next_token_size =
                    i + 1 >= results.size()
                        ? 0
                        : fidl_serialization::GetByteVectorSize(
                              context->entries->at(i + 1).key->size());
                if (!results[i]) {
                  size_t entry_size = ComputeEntrySize(entry);
                  if (real_size + entry_size + next_token_size >
                      fidl_serialization::kMaxInlineDataSize) {
                    break;
                  }
                  real_size += entry_size;
                  // We don't have the object locally, but we decided not to
                  // abort. This means this object is a value of a lazy key and
                  // the client should ask to retrieve it over the network if
                  // they need it. Here, we just leave the value part of the
                  // entry null.
                  continue;
                }

                storage::Status read_status =
                    FillSingleEntry(*results[i], &entry);
                if (read_status != storage::Status::OK) {
                  callback(PageUtils::ConvertStatus(read_status),
                           fidl::VectorPtr<EntryType>::New(0), nullptr);
                  return;
                }
                size_t entry_size = ComputeEntrySize(entry);
                if (real_size + entry_size + next_token_size >
                    fidl_serialization::kMaxInlineDataSize) {
                  break;
                }
                real_size += entry_size;
              }
              if (i != results.size()) {
                if (i == 0) {
                  callback(Status::VALUE_TOO_LARGE,
                           fidl::VectorPtr<EntryType>::New(0), nullptr);
                  return;
                }
                // We had to bail out early because the result would be too big
                // otherwise.
                context->next_token = std::make_unique<Token>();
                context->next_token->opaque_id =
                    std::move(context->entries->at(i).key);
                context->entries.resize(i);
              }
              if (context->next_token) {
                callback(Status::PARTIAL_RESULT, std::move(context->entries),
                         std::move(context->next_token));
                return;
              }
              callback(Status::OK, std::move(context->entries), nullptr);
            };
    waiter->Finalize(std::move(result_callback));
  };
  page_storage->GetCommitContents(*commit, std::move(start), std::move(on_next),
                                  std::move(on_done));
}
}  // namespace

PageSnapshotImpl::PageSnapshotImpl(
    storage::PageStorage* page_storage,
    std::unique_ptr<const storage::Commit> commit, std::string key_prefix)
    : page_storage_(page_storage),
      commit_(std::move(commit)),
      key_prefix_(std::move(key_prefix)) {}

PageSnapshotImpl::~PageSnapshotImpl() {}

void PageSnapshotImpl::GetEntries(fidl::VectorPtr<uint8_t> key_start,
                                  std::unique_ptr<Token> token,
                                  GetEntriesCallback callback) {
  FillEntries<Entry>(page_storage_, key_prefix_, commit_.get(),
                     std::move(key_start), std::move(token),
                     std::move(callback));
}

void PageSnapshotImpl::GetEntriesInline(fidl::VectorPtr<uint8_t> key_start,
                                        std::unique_ptr<Token> token,
                                        GetEntriesInlineCallback callback) {
  FillEntries<InlinedEntry>(page_storage_, key_prefix_, commit_.get(),
                            std::move(key_start), std::move(token),
                            std::move(callback));
}

void PageSnapshotImpl::GetKeys(fidl::VectorPtr<uint8_t> key_start,
                               std::unique_ptr<Token> token,
                               GetKeysCallback callback) {
  // Represents the information that needs to be shared between on_next and
  // on_done callbacks.
  struct Context {
    // The result of GetKeys. New keys from on_next are appended to this array.
    fidl::VectorPtr<fidl::VectorPtr<uint8_t>> keys =
        fidl::VectorPtr<fidl::VectorPtr<uint8_t>>::New(0);
    // The total size in number of bytes of the |keys| array.
    size_t size = fidl_serialization::kVectorHeaderSize;
    // If the |keys| array size exceeds the maximum allowed inlined data size,
    // |next_token| will have the value of the next key (not included in array)
    // which can be used as the next token.
    std::unique_ptr<Token> next_token;
  };

  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "snapshot_get_keys");

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
                  callback =
                      std::move(timed_callback)](storage::Status status) {
    if (status != storage::Status::OK) {
      FXL_LOG(ERROR) << "Error while reading: " << status;
      callback(Status::IO_ERROR,
               fidl::VectorPtr<fidl::VectorPtr<uint8_t>>::New(0), nullptr);
      return;
    }
    if (context->next_token) {
      callback(Status::PARTIAL_RESULT, std::move(context->keys),
               std::move(context->next_token));
    } else {
      callback(Status::OK, std::move(context->keys), nullptr);
    }
  };
  if (token) {
    page_storage_->GetCommitContents(*commit_,
                                     convert::ToString(token->opaque_id),
                                     std::move(on_next), std::move(on_done));
  } else {
    page_storage_->GetCommitContents(
        *commit_, std::max(convert::ToString(key_start), key_prefix_),
        std::move(on_next), std::move(on_done));
  }
}

void PageSnapshotImpl::Get(fidl::VectorPtr<uint8_t> key, GetCallback callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "snapshot_get");

  page_storage_->GetEntryFromCommit(
      *commit_, convert::ToString(key),
      [this, callback = std::move(timed_callback)](
          storage::Status status, storage::Entry entry) mutable {
        if (status != storage::Status::OK) {
          callback(PageUtils::ConvertStatus(status, Status::KEY_NOT_FOUND),
                   nullptr);
          return;
        }
        PageUtils::ResolveObjectIdentifierAsBuffer(
            page_storage_, entry.object_identifier, 0u,
            std::numeric_limits<int64_t>::max(),
            storage::PageStorage::Location::LOCAL, Status::NEEDS_FETCH,
            [callback = std::move(callback)](Status status,
                                             fsl::SizedVmo data) {
              callback(status, ToOptionalTransport(status, std::move(data)));
            });
      });
}

void PageSnapshotImpl::GetInline(fidl::VectorPtr<uint8_t> key,
                                 GetInlineCallback callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "snapshot_get_inline");

  page_storage_->GetEntryFromCommit(
      *commit_, convert::ToString(key),
      [this, callback = std::move(timed_callback)](
          storage::Status status, storage::Entry entry) mutable {
        if (status != storage::Status::OK) {
          callback(PageUtils::ConvertStatus(status, Status::KEY_NOT_FOUND),
                   nullptr);
          return;
        }
        PageUtils::ResolveObjectIdentifierAsStringView(
            page_storage_, entry.object_identifier,
            storage::PageStorage::Location::LOCAL, Status::NEEDS_FETCH,
            [callback = std::move(callback)](Status status,
                                             fxl::StringView data_view) {
              if (status != Status::OK) {
                callback(status, nullptr);
                return;
              }
              if (fidl_serialization::GetByteVectorSize(data_view.size()) +
                      fidl_serialization::kStatusEnumSize >
                  fidl_serialization::kMaxInlineDataSize) {
                callback(Status::VALUE_TOO_LARGE, nullptr);
                return;
              }
              auto inlined_value = std::make_unique<InlinedValue>();
              inlined_value->value = convert::ToArray(data_view);
              callback(status, std::move(inlined_value));
            });
      });
}

void PageSnapshotImpl::Fetch(fidl::VectorPtr<uint8_t> key,
                             FetchCallback callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "snapshot_fetch");

  page_storage_->GetEntryFromCommit(
      *commit_, convert::ToString(key),
      [this, callback = std::move(timed_callback)](
          storage::Status status, storage::Entry entry) mutable {
        if (status != storage::Status::OK) {
          callback(PageUtils::ConvertStatus(status, Status::KEY_NOT_FOUND),
                   nullptr);
          return;
        }
        PageUtils::ResolveObjectIdentifierAsBuffer(
            page_storage_, entry.object_identifier, 0u,
            std::numeric_limits<int64_t>::max(),
            storage::PageStorage::Location::NETWORK, Status::INTERNAL_ERROR,
            [callback = std::move(callback)](Status status,
                                             fsl::SizedVmo data) {
              callback(status, ToOptionalTransport(status, std::move(data)));
            });
      });
}

void PageSnapshotImpl::FetchPartial(fidl::VectorPtr<uint8_t> key,
                                    int64_t offset, int64_t max_size,
                                    FetchPartialCallback callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "snapshot_fetch_partial");

  page_storage_->GetEntryFromCommit(
      *commit_, convert::ToString(key),
      [this, offset, max_size, callback = std::move(timed_callback)](
          storage::Status status, storage::Entry entry) mutable {
        if (status != storage::Status::OK) {
          callback(PageUtils::ConvertStatus(status, Status::KEY_NOT_FOUND),
                   nullptr);
          return;
        }

        PageUtils::ResolveObjectIdentifierAsBuffer(
            page_storage_, entry.object_identifier, offset, max_size,
            storage::PageStorage::Location::NETWORK, Status::INTERNAL_ERROR,
            [callback = std::move(callback)](Status status,
                                             fsl::SizedVmo data) {
              callback(status, ToOptionalTransport(status, std::move(data)));
            });
      });
}

}  // namespace ledger
