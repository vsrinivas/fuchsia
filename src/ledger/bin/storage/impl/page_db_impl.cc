// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/page_db_impl.h"

#include <algorithm>
#include <iterator>
#include <string>

#include "peridot/lib/convert/convert.h"
#include "src/ledger/bin/storage/impl/data_serialization.h"
#include "src/ledger/bin/storage/impl/db_serialization.h"
#include "src/ledger/bin/storage/impl/object_identifier_encoding.h"
#include "src/ledger/bin/storage/impl/object_impl.h"
#include "src/ledger/bin/storage/impl/page_db_batch_impl.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/concatenate.h"

#define RETURN_ON_ERROR(expr)   \
  do {                          \
    Status status = (expr);     \
    if (status != Status::OK) { \
      return status;            \
    }                           \
  } while (0)

namespace storage {

using coroutine::CoroutineHandler;

namespace {

// Extracts a sorted list of deserialized |A|'s to commit ids from |entries|.
// Entries must be a map from commit ids to serialized |A|.
template <typename A>
void ExtractSortedCommitsIds(
    std::vector<std::pair<std::string, std::string>>* entries,
    std::vector<std::pair<A, CommitId>>* commit_ids) {
  commit_ids->clear();
  commit_ids->reserve(entries->size());
  std::transform(std::make_move_iterator(entries->begin()),
                 std::make_move_iterator(entries->end()),
                 std::back_inserter(*commit_ids),
                 [](std::pair<std::string, std::string>&& entry) {
                   auto t = DeserializeData<A>(entry.second);
                   return std::make_pair(t, std::move(entry.first));
                 });
  std::sort(commit_ids->begin(), commit_ids->end());
}

}  // namespace

PageDbImpl::PageDbImpl(ledger::Environment* environment, std::unique_ptr<Db> db)
    : environment_(environment), db_(std::move(db)) {
  FXL_DCHECK(environment_);
  FXL_DCHECK(db_);
}

PageDbImpl::~PageDbImpl() {}

Status PageDbImpl::StartBatch(coroutine::CoroutineHandler* handler,
                              std::unique_ptr<Batch>* batch) {
  std::unique_ptr<Db::Batch> db_batch;
  RETURN_ON_ERROR(db_->StartBatch(handler, &db_batch));
  *batch = std::make_unique<PageDbBatchImpl>(std::move(db_batch), this);
  return Status::OK;
}

Status PageDbImpl::GetHeads(
    CoroutineHandler* handler,
    std::vector<std::pair<zx::time_utc, CommitId>>* heads) {
  std::vector<std::pair<std::string, std::string>> entries;
  RETURN_ON_ERROR(db_->GetEntriesByPrefix(
      handler, convert::ToSlice(HeadRow::kPrefix), &entries));
  ExtractSortedCommitsIds<zx::time_utc>(&entries, heads);
  return Status::OK;
}

Status PageDbImpl::GetMerges(coroutine::CoroutineHandler* handler,
                             CommitIdView commit1_id, CommitIdView commit2_id,
                             std::vector<CommitId>* merges) {
  merges->clear();
  return db_->GetByPrefix(
      handler, MergeRow::GetEntriesPrefixFor(commit1_id, commit2_id), merges);
}

Status PageDbImpl::GetCommitStorageBytes(CoroutineHandler* handler,
                                         CommitIdView commit_id,
                                         std::string* storage_bytes) {
  return db_->Get(handler, CommitRow::GetKeyFor(commit_id), storage_bytes);
}

Status PageDbImpl::ReadObject(CoroutineHandler* handler,
                              const ObjectIdentifier& object_identifier,
                              std::unique_ptr<const Piece>* piece) {
  FXL_DCHECK(piece);
  return db_->GetObject(handler,
                        ObjectRow::GetKeyFor(object_identifier.object_digest()),
                        object_identifier, piece);
}

Status PageDbImpl::HasObject(CoroutineHandler* handler,
                             const ObjectIdentifier& object_identifier) {
  return db_->HasKey(handler,
                     ObjectRow::GetKeyFor(object_identifier.object_digest()));
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
       {PageDbObjectStatus::SYNCED, PageDbObjectStatus::TRANSIENT,
        PageDbObjectStatus::LOCAL, PageDbObjectStatus::SYNCED}) {
    Status key_found_status = db_->HasKey(
        handler,
        ObjectStatusRow::GetKeyFor(possible_status, object_identifier));
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

Status PageDbImpl::GetInboundObjectReferences(
    coroutine::CoroutineHandler* handler,
    const ObjectIdentifier& object_identifier,
    ObjectReferencesAndPriority* references) {
  FXL_DCHECK(references);
  references->clear();
  std::vector<std::string> keys;
  RETURN_ON_ERROR(db_->GetByPrefix(
      handler,
      ReferenceRow::GetEagerKeyPrefixFor(object_identifier.object_digest()),
      &keys));
  for (auto& key : keys) {
    references->emplace(ObjectDigest(std::move(key)), KeyPriority::EAGER);
  }
  RETURN_ON_ERROR(db_->GetByPrefix(
      handler,
      ReferenceRow::GetLazyKeyPrefixFor(object_identifier.object_digest()),
      &keys));
  for (auto& key : keys) {
    references->emplace(ObjectDigest(std::move(key)), KeyPriority::LAZY);
  }
  return Status::OK;
}

Status PageDbImpl::GetInboundCommitReferences(
    coroutine::CoroutineHandler* handler,
    const ObjectIdentifier& object_identifier,
    std::vector<CommitId>* references) {
  FXL_DCHECK(references);
  references->clear();
  return db_->GetByPrefix(
      handler,
      ReferenceRow::GetCommitKeyPrefixFor(object_identifier.object_digest()),
      references);
}

Status PageDbImpl::GetUnsyncedCommitIds(CoroutineHandler* handler,
                                        std::vector<CommitId>* commit_ids) {
  std::vector<std::pair<std::string, std::string>> entries;
  RETURN_ON_ERROR(db_->GetEntriesByPrefix(
      handler, convert::ToSlice(UnsyncedCommitRow::kPrefix), &entries));
  // Unsynced commit row values are the commit's generation.
  std::vector<std::pair<uint64_t, CommitId>> extracted_ids;
  ExtractSortedCommitsIds<uint64_t>(&entries, &extracted_ids);
  commit_ids->clear();
  commit_ids->reserve(entries.size());
  std::transform(std::make_move_iterator(extracted_ids.begin()),
                 std::make_move_iterator(extracted_ids.end()),
                 std::back_inserter(*commit_ids),
                 [](std::pair<uint64_t, CommitId>&& commit_id_pair) {
                   return std::move(std::get<CommitId>(commit_id_pair));
                 });
  return Status::OK;
}

Status PageDbImpl::IsCommitSynced(CoroutineHandler* handler,
                                  const CommitId& commit_id, bool* is_synced) {
  Status status = db_->HasKey(handler, UnsyncedCommitRow::GetKeyFor(commit_id));
  if (status != Status::OK && status != Status::INTERNAL_NOT_FOUND) {
    return status;
  }
  *is_synced = (status == Status::INTERNAL_NOT_FOUND);
  return Status::OK;
}

Status PageDbImpl::GetUnsyncedPieces(
    CoroutineHandler* handler,
    std::vector<ObjectIdentifier>* object_identifiers) {
  std::vector<std::string> encoded_identifiers;
  Status status =
      db_->GetByPrefix(handler, convert::ToSlice(ObjectStatusRow::kLocalPrefix),
                       &encoded_identifiers);
  if (status != Status::OK) {
    return status;
  }

  object_identifiers->clear();
  ObjectIdentifier object_identifier;
  for (auto& encoded_identifier : encoded_identifiers) {
    if (!DecodeObjectIdentifier(encoded_identifier, &object_identifier)) {
      return Status::FORMAT_ERROR;
    }
    object_identifiers->emplace_back(std::move(object_identifier));
  }

  return Status::OK;
}

Status PageDbImpl::GetSyncMetadata(CoroutineHandler* handler,
                                   fxl::StringView key, std::string* value) {
  return db_->Get(handler, SyncMetadataRow::GetKeyFor(key), value);
}

Status PageDbImpl::IsPageOnline(coroutine::CoroutineHandler* handler,
                                bool* page_is_online) {
  Status status = db_->HasKey(handler, PageIsOnlineRow::kKey);
  if (status != Status::OK && status != Status::INTERNAL_NOT_FOUND) {
    return status;
  }
  *page_is_online = (status == Status::OK);
  return Status::OK;
}

Status PageDbImpl::AddHead(CoroutineHandler* handler, CommitIdView head,
                           zx::time_utc timestamp) {
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

Status PageDbImpl::AddMerge(coroutine::CoroutineHandler* handler,
                            CommitIdView parent1_id, CommitIdView parent2_id,
                            CommitIdView merge_commit_id) {
  // This should only be called in a batch.
  return Status::ILLEGAL_STATE;
}

Status PageDbImpl::AddCommitStorageBytes(CoroutineHandler* handler,
                                         const CommitId& commit_id,
                                         const ObjectIdentifier& root_node,
                                         fxl::StringView storage_bytes) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->AddCommitStorageBytes(handler, commit_id, root_node,
                                               storage_bytes));
  return batch->Execute(handler);
}

Status PageDbImpl::WriteObject(CoroutineHandler* handler, const Piece& piece,
                               PageDbObjectStatus object_status,
                               const ObjectReferencesAndPriority& references) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(
      batch->WriteObject(handler, piece, object_status, references));
  return batch->Execute(handler);
}

Status PageDbImpl::SetObjectStatus(CoroutineHandler* handler,
                                   const ObjectIdentifier& object_identifier,
                                   PageDbObjectStatus object_status) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(
      batch->SetObjectStatus(handler, object_identifier, object_status));
  return batch->Execute(handler);
}

Status PageDbImpl::MarkCommitIdSynced(CoroutineHandler* handler,
                                      const CommitId& commit_id) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->MarkCommitIdSynced(handler, commit_id));
  return batch->Execute(handler);
}

Status PageDbImpl::MarkCommitIdUnsynced(CoroutineHandler* handler,
                                        const CommitId& commit_id,
                                        uint64_t generation) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->MarkCommitIdUnsynced(handler, commit_id, generation));
  return batch->Execute(handler);
}

Status PageDbImpl::SetSyncMetadata(CoroutineHandler* handler,
                                   fxl::StringView key, fxl::StringView value) {
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

}  // namespace storage
