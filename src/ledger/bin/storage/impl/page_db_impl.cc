// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/page_db_impl.h"

#include <algorithm>
#include <iterator>
#include <string>

#include "src/ledger/bin/storage/impl/clock_serialization.h"
#include "src/ledger/bin/storage/impl/constants.h"
#include "src/ledger/bin/storage/impl/data_serialization.h"
#include "src/ledger/bin/storage/impl/db_serialization.h"
#include "src/ledger/bin/storage/impl/object_identifier_encoding.h"
#include "src/ledger/bin/storage/impl/object_impl.h"
#include "src/ledger/bin/storage/impl/page_db_batch_impl.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/fxl/logging.h"
#include "third_party/abseil-cpp/absl/strings/str_cat.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {

using coroutine::CoroutineHandler;

namespace {

// Extracts a sorted list of deserialized |A|'s to commit ids from |entries|.
// Entries must be a map from commit ids to serialized |A|.
template <typename A>
void ExtractSortedCommitsIds(std::vector<std::pair<std::string, std::string>>* entries,
                             std::vector<std::pair<A, CommitId>>* commit_ids) {
  commit_ids->clear();
  commit_ids->reserve(entries->size());
  std::transform(std::make_move_iterator(entries->begin()), std::make_move_iterator(entries->end()),
                 std::back_inserter(*commit_ids), [](std::pair<std::string, std::string>&& entry) {
                   auto t = DeserializeData<A>(entry.second);
                   return std::make_pair(t, std::move(entry.first));
                 });
  std::sort(commit_ids->begin(), commit_ids->end());
}

}  // namespace

PageDbImpl::PageDbImpl(ledger::Environment* environment,
                       ObjectIdentifierFactory* object_identifier_factory, std::unique_ptr<Db> db)
    : environment_(environment),
      object_identifier_factory_(object_identifier_factory),
      db_(std::move(db)) {
  FXL_DCHECK(environment_);
  FXL_DCHECK(object_identifier_factory_);
  FXL_DCHECK(db_);
}

PageDbImpl::~PageDbImpl() = default;

Status PageDbImpl::StartBatch(coroutine::CoroutineHandler* handler, std::unique_ptr<Batch>* batch) {
  std::unique_ptr<Db::Batch> db_batch;
  RETURN_ON_ERROR(db_->StartBatch(handler, &db_batch));
  *batch = std::make_unique<PageDbBatchImpl>(std::move(db_batch), this, object_identifier_factory_);
  return Status::OK;
}

Status PageDbImpl::GetHeads(CoroutineHandler* handler,
                            std::vector<std::pair<zx::time_utc, CommitId>>* heads) {
  std::vector<std::pair<std::string, std::string>> entries;
  RETURN_ON_ERROR(db_->GetEntriesByPrefix(handler, convert::ToSlice(HeadRow::kPrefix), &entries));
  ExtractSortedCommitsIds<zx::time_utc>(&entries, heads);
  return Status::OK;
}

Status PageDbImpl::GetMerges(coroutine::CoroutineHandler* handler, CommitIdView commit1_id,
                             CommitIdView commit2_id, std::vector<CommitId>* merges) {
  merges->clear();
  return db_->GetByPrefix(handler, MergeRow::GetEntriesPrefixFor(commit1_id, commit2_id), merges);
}

Status PageDbImpl::GetCommitStorageBytes(CoroutineHandler* handler, CommitIdView commit_id,
                                         std::string* storage_bytes) {
  return db_->Get(handler, CommitRow::GetKeyFor(commit_id), storage_bytes);
}

Status PageDbImpl::ReadObject(CoroutineHandler* handler, const ObjectIdentifier& object_identifier,
                              std::unique_ptr<const Piece>* piece) {
  FXL_DCHECK(piece);
  const Status status = db_->GetObject(
      handler, ObjectRow::GetKeyFor(object_identifier.object_digest()), object_identifier, piece);
  return status;
}

Status PageDbImpl::HasObject(CoroutineHandler* handler, const ObjectIdentifier& object_identifier) {
  return db_->HasKey(handler, ObjectRow::GetKeyFor(object_identifier.object_digest()));
}

Status PageDbImpl::GetObjectStatus(CoroutineHandler* handler,
                                   const ObjectIdentifier& object_identifier,
                                   PageDbObjectStatus* object_status) {
  // Check must be done in ascending order of status, so that a change of status
  // between 2 reads does not create the case where no key is found.
  // That said, the most common expected status is SYNCED, so for performance
  // reasons, it is better to check it first.
  // By checking it first and then checking all statuses in ascending order we
  // both ensure correctness and performant lookup.
  // The only case that would generate a spurious lookup is when the status is
  // changed concurrently, which is a rare occurence.
  for (PageDbObjectStatus possible_status :
       {PageDbObjectStatus::SYNCED, PageDbObjectStatus::TRANSIENT, PageDbObjectStatus::LOCAL,
        PageDbObjectStatus::SYNCED}) {
    Status key_found_status =
        db_->HasKey(handler, ObjectStatusRow::GetKeyFor(possible_status, object_identifier));
    if (key_found_status == Status::OK) {
      *object_status = possible_status;
      return Status::OK;
    }
    if (key_found_status != Status::INTERNAL_NOT_FOUND) {
      return key_found_status;
    }
  }

  *object_status = PageDbObjectStatus::UNKNOWN;
  return Status::OK;
}

Status PageDbImpl::GetObjectStatusKeys(coroutine::CoroutineHandler* handler,
                                       const ObjectDigest& object_digest,
                                       std::map<std::string, PageDbObjectStatus>* keys) {
  keys->clear();
  // Check must be done in ascending order of status, so that a change of status between 2 reads
  // does not create the case where no key is found.
  for (PageDbObjectStatus possible_status :
       {PageDbObjectStatus::TRANSIENT, PageDbObjectStatus::LOCAL, PageDbObjectStatus::SYNCED}) {
    std::string prefix = ObjectStatusRow::GetPrefixFor(possible_status, object_digest);
    std::vector<std::string> suffixes;
    RETURN_ON_ERROR(db_->GetByPrefix(handler, prefix, &suffixes));
    for (const std::string& suffix : suffixes) {
      (*keys)[absl::StrCat(prefix, suffix)] = possible_status;
    }
  }
  return Status::OK;
}

Status PageDbImpl::GetInboundObjectReferences(coroutine::CoroutineHandler* handler,
                                              const ObjectIdentifier& object_identifier,
                                              ObjectReferencesAndPriority* references) {
  FXL_DCHECK(references);
  references->clear();
  std::vector<std::string> keys;
  RETURN_ON_ERROR(db_->GetByPrefix(
      handler, ReferenceRow::GetEagerKeyPrefixFor(object_identifier.object_digest()), &keys));
  for (auto& key : keys) {
    references->emplace(ObjectDigest(std::move(key)), KeyPriority::EAGER);
  }
  RETURN_ON_ERROR(db_->GetByPrefix(
      handler, ReferenceRow::GetLazyKeyPrefixFor(object_identifier.object_digest()), &keys));
  for (auto& key : keys) {
    references->emplace(ObjectDigest(std::move(key)), KeyPriority::LAZY);
  }
  return Status::OK;
}

Status PageDbImpl::GetInboundCommitReferences(coroutine::CoroutineHandler* handler,
                                              const ObjectIdentifier& object_identifier,
                                              std::vector<CommitId>* references) {
  FXL_DCHECK(references);
  references->clear();
  return db_->GetByPrefix(
      handler, ReferenceRow::GetCommitKeyPrefixFor(object_identifier.object_digest()), references);
}

Status PageDbImpl::EnsureObjectDeletable(coroutine::CoroutineHandler* handler,
                                         const ObjectDigest& object_digest,
                                         std::vector<std::string>* object_status_keys) {
  FXL_DCHECK(object_status_keys);

  // If there is any object-object reference to the object, it cannot be garbage collected.
  Status status = db_->HasPrefix(handler, ReferenceRow::GetObjectKeyPrefixFor(object_digest));
  if (status == Status::OK) {
    return Status::CANCELED;
  }
  if (status != Status::INTERNAL_NOT_FOUND) {
    return status;
  }

  std::map<std::string, PageDbObjectStatus> keys;
  RETURN_ON_ERROR(GetObjectStatusKeys(handler, object_digest, &keys));
  // object-object references have already been checked.  Collect object status keys, and check if
  // any of them requires checking commit-object links.
  bool check_commit_object_refs = false;
  for (const auto& [object_status_key, object_status] : keys) {
    object_status_keys->push_back(object_status_key);
    switch (object_status) {
      case PageDbObjectStatus::UNKNOWN:
        FXL_NOTREACHED();
        return Status::INTERNAL_ERROR;
      case PageDbObjectStatus::TRANSIENT:
      case PageDbObjectStatus::LOCAL:
        // object-object and commit-object links must both be zero for transient and local objects.
        check_commit_object_refs = true;
        break;
      case PageDbObjectStatus::SYNCED:
        // Only object-object links are relevant for synced objects.
        break;
    }
  }
  if (check_commit_object_refs) {
    Status status = db_->HasPrefix(handler, ReferenceRow::GetKeyPrefixFor(object_digest));
    if (status == Status::OK) {
      return Status::CANCELED;
    }
    if (status != Status::INTERNAL_NOT_FOUND) {
      return status;
    }
  }
  return Status::OK;
}

Status PageDbImpl::GetUnsyncedCommitIds(CoroutineHandler* handler,
                                        std::vector<CommitId>* commit_ids) {
  std::vector<std::pair<std::string, std::string>> entries;
  RETURN_ON_ERROR(
      db_->GetEntriesByPrefix(handler, convert::ToSlice(UnsyncedCommitRow::kPrefix), &entries));
  // Unsynced commit row values are the commit's generation.
  std::vector<std::pair<uint64_t, CommitId>> extracted_ids;
  ExtractSortedCommitsIds<uint64_t>(&entries, &extracted_ids);
  commit_ids->clear();
  commit_ids->reserve(entries.size());
  std::transform(std::make_move_iterator(extracted_ids.begin()),
                 std::make_move_iterator(extracted_ids.end()), std::back_inserter(*commit_ids),
                 [](std::pair<uint64_t, CommitId>&& commit_id_pair) {
                   return std::move(std::get<CommitId>(commit_id_pair));
                 });
  return Status::OK;
}

Status PageDbImpl::IsCommitSynced(CoroutineHandler* handler, const CommitId& commit_id,
                                  bool* is_synced) {
  Status status = db_->HasKey(handler, UnsyncedCommitRow::GetKeyFor(commit_id));
  if (status != Status::OK && status != Status::INTERNAL_NOT_FOUND) {
    return status;
  }
  *is_synced = (status == Status::INTERNAL_NOT_FOUND);
  return Status::OK;
}

Status PageDbImpl::GetUnsyncedPieces(CoroutineHandler* handler,
                                     std::vector<ObjectIdentifier>* object_identifiers) {
  std::vector<std::string> encoded_identifiers;
  RETURN_ON_ERROR(db_->GetByPrefix(handler, convert::ToSlice(ObjectStatusRow::kLocalPrefix),
                                   &encoded_identifiers));

  object_identifiers->clear();
  ObjectIdentifier object_identifier;
  for (auto& encoded_identifier : encoded_identifiers) {
    if (!DecodeDigestPrefixedObjectIdentifier(encoded_identifier, object_identifier_factory_,
                                              &object_identifier)) {
      return Status::DATA_INTEGRITY_ERROR;
    }
    object_identifiers->emplace_back(std::move(object_identifier));
  }

  return Status::OK;
}

Status PageDbImpl::GetSyncMetadata(CoroutineHandler* handler, absl::string_view key,
                                   std::string* value) {
  return db_->Get(handler, SyncMetadataRow::GetKeyFor(key), value);
}

Status PageDbImpl::IsPageOnline(coroutine::CoroutineHandler* handler, bool* page_is_online) {
  Status status = db_->HasKey(handler, PageIsOnlineRow::kKey);
  if (status != Status::OK && status != Status::INTERNAL_NOT_FOUND) {
    return status;
  }
  *page_is_online = (status == Status::OK);
  return Status::OK;
}

Status PageDbImpl::AddHead(CoroutineHandler* handler, CommitIdView head, zx::time_utc timestamp) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->AddHead(handler, head, timestamp));
  return batch->Execute(handler);
}

Status PageDbImpl::RemoveHead(CoroutineHandler* handler, CommitIdView head) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->RemoveHead(handler, head));
  return batch->Execute(handler);
}

Status PageDbImpl::AddMerge(coroutine::CoroutineHandler* handler, CommitIdView parent1_id,
                            CommitIdView parent2_id, CommitIdView merge_commit_id) {
  // This should only be called in a batch.
  return Status::ILLEGAL_STATE;
}

Status PageDbImpl::DeleteMerge(coroutine::CoroutineHandler* handler, CommitIdView parent1_id,
                               CommitIdView parent2_id, CommitIdView commit_id) {
  // This should only be called in a batch.
  return Status::ILLEGAL_STATE;
}

Status PageDbImpl::AddCommitStorageBytes(CoroutineHandler* handler, const CommitId& commit_id,
                                         absl::string_view remote_commit_id,
                                         const ObjectIdentifier& root_node,
                                         absl::string_view storage_bytes) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(
      batch->AddCommitStorageBytes(handler, commit_id, remote_commit_id, root_node, storage_bytes));
  return batch->Execute(handler);
}

Status PageDbImpl::DeleteCommit(coroutine::CoroutineHandler* handler, CommitIdView commit_id,
                                absl::string_view remote_commit_id,
                                const ObjectIdentifier& root_node) {
  // This should only be called in a batch.
  return Status::ILLEGAL_STATE;
}

Status PageDbImpl::WriteObject(CoroutineHandler* handler, const Piece& piece,
                               PageDbObjectStatus object_status,
                               const ObjectReferencesAndPriority& references) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->WriteObject(handler, piece, object_status, references));
  return batch->Execute(handler);
}

Status PageDbImpl::DeleteObject(coroutine::CoroutineHandler* handler,
                                const ObjectDigest& object_digest,
                                const ObjectReferencesAndPriority& references) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->DeleteObject(handler, object_digest, references));
  return batch->Execute(handler);
}

Status PageDbImpl::SetObjectStatus(CoroutineHandler* handler,
                                   const ObjectIdentifier& object_identifier,
                                   PageDbObjectStatus object_status) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->SetObjectStatus(handler, object_identifier, object_status));
  return batch->Execute(handler);
}

Status PageDbImpl::MarkCommitIdSynced(CoroutineHandler* handler, const CommitId& commit_id) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->MarkCommitIdSynced(handler, commit_id));
  return batch->Execute(handler);
}

Status PageDbImpl::MarkCommitIdUnsynced(CoroutineHandler* handler, const CommitId& commit_id,
                                        uint64_t generation) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->MarkCommitIdUnsynced(handler, commit_id, generation));
  return batch->Execute(handler);
}

Status PageDbImpl::SetSyncMetadata(CoroutineHandler* handler, absl::string_view key,
                                   absl::string_view value) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->SetSyncMetadata(handler, key, value));
  return batch->Execute(handler);
}

Status PageDbImpl::MarkPageOnline(coroutine::CoroutineHandler* handler) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->MarkPageOnline(handler));
  return batch->Execute(handler);
}

Status PageDbImpl::GetDeviceId(coroutine::CoroutineHandler* handler, clocks::DeviceId* device_id) {
  std::string data;
  RETURN_ON_ERROR(db_->Get(handler, ClockRow::kDeviceIdKey, &data));
  if (!ExtractDeviceIdFromStorage(std::move(data), device_id)) {
    return Status::INTERNAL_ERROR;
  }
  return Status::OK;
}

Status PageDbImpl::GetClock(coroutine::CoroutineHandler* handler, Clock* clock) {
  std::string data;
  RETURN_ON_ERROR(db_->Get(handler, ClockRow::kEntriesKey, &data));
  if (!ExtractClockFromStorage(std::move(data), clock)) {
    return Status::INTERNAL_ERROR;
  }
  return Status::OK;
}

Status PageDbImpl::SetDeviceId(coroutine::CoroutineHandler* handler,
                               const clocks::DeviceId& device_id) {
  // clocks::DeviceId should not be set.
  RETURN_ON_ERROR(DCheckDeviceIdNotSet(handler));

  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->SetDeviceId(handler, device_id));
  return batch->Execute(handler);
}

Status PageDbImpl::SetClock(coroutine::CoroutineHandler* handler, const Clock& entry) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->SetClock(handler, entry));
  return batch->Execute(handler);
}

Status PageDbImpl::GetCommitIdFromRemoteId(coroutine::CoroutineHandler* handler,
                                           absl::string_view remote_id, CommitId* commit_id) {
  return db_->Get(handler, RemoteCommitIdToLocalRow::GetKeyFor(remote_id), commit_id);
}

Status PageDbImpl::DCheckDeviceIdNotSet(coroutine::CoroutineHandler* handler) {
#ifdef NDEBUG
  return Status::OK;
#else
  Status status = db_->HasKey(handler, ClockRow::kDeviceIdKey);
  if (status == Status::INTERRUPTED) {
    return status;
  }
  FXL_DCHECK(status == Status::INTERNAL_NOT_FOUND);
  return Status::OK;
#endif
}

}  // namespace storage
