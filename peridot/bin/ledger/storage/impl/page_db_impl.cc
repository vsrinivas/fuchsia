// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/page_db_impl.h"

#include <algorithm>
#include <string>

#include <lib/fxl/strings/concatenate.h>

#include "peridot/bin/ledger/storage/impl/data_serialization.h"
#include "peridot/bin/ledger/storage/impl/db_serialization.h"
#include "peridot/bin/ledger/storage/impl/object_identifier_encoding.h"
#include "peridot/bin/ledger/storage/impl/object_impl.h"
#include "peridot/bin/ledger/storage/impl/page_db_batch_impl.h"
#include "peridot/lib/convert/convert.h"

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

// Extracts a sorted list of commit its from |entries|. Entries must be a map
// from commit ids to serialized |A|.
template <typename A>
void ExtractSortedCommitsIds(
    std::vector<std::pair<std::string, std::string>>* entries,
    std::vector<CommitId>* commit_ids) {
  std::sort(entries->begin(), entries->end(),
            [&](const std::pair<std::string, std::string>& p1,
                const std::pair<std::string, std::string>& p2) {
              auto t1 = DeserializeData<A>(p1.second);
              auto t2 = DeserializeData<A>(p2.second);
              if (t1 != t2) {
                return t1 < t2;
              }
              return p1.first < p2.first;
            });
  commit_ids->clear();
  commit_ids->reserve(entries->size());
  for (std::pair<std::string, std::string>& entry : *entries) {
    commit_ids->push_back(std::move(entry.first));
  }
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

Status PageDbImpl::GetHeads(CoroutineHandler* handler,
                            std::vector<CommitId>* heads) {
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
                              ObjectIdentifier object_identifier,
                              std::unique_ptr<const Object>* object) {
  return db_->GetObject(handler,
                        ObjectRow::GetKeyFor(object_identifier.object_digest()),
                        object_identifier, object);
}

Status PageDbImpl::HasObject(CoroutineHandler* handler,
                             const ObjectDigest& object_digest,
                             bool* has_object) {
  return db_->HasKey(handler, ObjectRow::GetKeyFor(object_digest), has_object);
}

Status PageDbImpl::GetObjectStatus(CoroutineHandler* handler,
                                   ObjectIdentifier object_identifier,
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
    bool has_key;
    RETURN_ON_ERROR(db_->HasKey(
        handler, ObjectStatusRow::GetKeyFor(possible_status, object_identifier),
        &has_key));
    if (has_key) {
      *object_status = possible_status;
      return Status::OK;
    }
  }

  *object_status = PageDbObjectStatus::UNKNOWN;
  return Status::OK;
}

Status PageDbImpl::GetUnsyncedCommitIds(CoroutineHandler* handler,
                                        std::vector<CommitId>* commit_ids) {
  std::vector<std::pair<std::string, std::string>> entries;
  RETURN_ON_ERROR(db_->GetEntriesByPrefix(
      handler, convert::ToSlice(UnsyncedCommitRow::kPrefix), &entries));
  // Unsynced commit row values are the commit's generation.
  ExtractSortedCommitsIds<uint64_t>(&entries, commit_ids);
  return Status::OK;
}

Status PageDbImpl::IsCommitSynced(CoroutineHandler* handler,
                                  const CommitId& commit_id, bool* is_synced) {
  bool has_key;
  RETURN_ON_ERROR(
      db_->HasKey(handler, UnsyncedCommitRow::GetKeyFor(commit_id), &has_key));
  *is_synced = !has_key;
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
  return db_->HasKey(handler, PageIsOnlineRow::kKey, page_is_online);
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
                                         fxl::StringView storage_bytes) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(
      batch->AddCommitStorageBytes(handler, commit_id, storage_bytes));
  return batch->Execute(handler);
}

Status PageDbImpl::RemoveCommit(CoroutineHandler* handler,
                                const CommitId& commit_id) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->RemoveCommit(handler, commit_id));
  return batch->Execute(handler);
}

Status PageDbImpl::WriteObject(CoroutineHandler* handler,
                               ObjectIdentifier object_identifier,
                               std::unique_ptr<DataSource::DataChunk> content,
                               PageDbObjectStatus object_status) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->WriteObject(handler, object_identifier,
                                     std::move(content), object_status));
  return batch->Execute(handler);
}

Status PageDbImpl::SetObjectStatus(CoroutineHandler* handler,
                                   ObjectIdentifier object_identifier,
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
