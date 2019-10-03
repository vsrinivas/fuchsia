// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/page_db_batch_impl.h"

#include <map>
#include <memory>

#include "src/ledger/bin/storage/impl/clock_serialization.h"
#include "src/ledger/bin/storage/impl/data_serialization.h"
#include "src/ledger/bin/storage/impl/db_serialization.h"
#include "src/ledger/bin/storage/impl/object_digest.h"
#include "src/ledger/bin/storage/impl/page_db.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/concatenate.h"

namespace storage {

using coroutine::CoroutineHandler;

PageDbBatchImpl::PageDbBatchImpl(std::unique_ptr<Db::Batch> batch, PageDb* page_db, Db* db,
                                 ObjectIdentifierFactory* factory)
    : batch_(std::move(batch)), page_db_(page_db), db_(db), factory_(factory) {}

PageDbBatchImpl::~PageDbBatchImpl() { UntrackPendingDeletions(); }

Status PageDbBatchImpl::AddHead(CoroutineHandler* handler, CommitIdView head,
                                zx::time_utc timestamp) {
  return batch_->Put(handler, HeadRow::GetKeyFor(head), SerializeData(timestamp));
}

Status PageDbBatchImpl::RemoveHead(CoroutineHandler* handler, CommitIdView head) {
  return batch_->Delete(handler, HeadRow::GetKeyFor(head));
}

Status PageDbBatchImpl::AddMerge(coroutine::CoroutineHandler* handler, CommitIdView parent1_id,
                                 CommitIdView parent2_id, CommitIdView merge_commit_id) {
  return batch_->Put(handler, MergeRow::GetKeyFor(parent1_id, parent2_id, merge_commit_id), "");
}

Status PageDbBatchImpl::DeleteMerge(coroutine::CoroutineHandler* handler, CommitIdView parent1_id,
                                    CommitIdView parent2_id, CommitIdView commit_id) {
  return batch_->Delete(handler, MergeRow::GetKeyFor(parent1_id, parent2_id, commit_id));
}

Status PageDbBatchImpl::AddCommitStorageBytes(CoroutineHandler* handler, const CommitId& commit_id,
                                              const ObjectIdentifier& root_node,
                                              fxl::StringView storage_bytes) {
  RETURN_ON_ERROR(batch_->Put(
      handler, ReferenceRow::GetKeyForCommit(commit_id, root_node.object_digest()), ""));
  return batch_->Put(handler, CommitRow::GetKeyFor(commit_id), storage_bytes);
}

Status PageDbBatchImpl::DeleteCommit(coroutine::CoroutineHandler* handler, CommitIdView commit_id,
                                     const ObjectIdentifier& root_node) {
  RETURN_ON_ERROR(
      batch_->Delete(handler, ReferenceRow::GetKeyForCommit(commit_id, root_node.object_digest())));
  RETURN_ON_ERROR(batch_->Delete(handler, UnsyncedCommitRow::GetKeyFor(commit_id)));
  return batch_->Delete(handler, CommitRow::GetKeyFor(commit_id));
}

Status PageDbBatchImpl::WriteObject(CoroutineHandler* handler, const Piece& piece,
                                    PageDbObjectStatus object_status,
                                    const ObjectReferencesAndPriority& references) {
  FXL_DCHECK(object_status > PageDbObjectStatus::UNKNOWN);

  const ObjectIdentifier& object_identifier = piece.GetIdentifier();
  Status status = page_db_->HasObject(handler, object_identifier);
  if (status == Status::OK) {
    if (object_status == PageDbObjectStatus::TRANSIENT) {
      return Status::OK;
    }
    return SetObjectStatus(handler, piece.GetIdentifier(), object_status);
  }
  if (status != Status::INTERNAL_NOT_FOUND) {
    return status;
  }

  RETURN_ON_ERROR(batch_->Put(handler, ObjectRow::GetKeyFor(object_identifier.object_digest()),
                              piece.GetData()));
  for (const auto& [child, priority] : references) {
    FXL_DCHECK(!GetObjectDigestInfo(child).is_inlined());
    RETURN_ON_ERROR(batch_->Put(
        handler, ReferenceRow::GetKeyForObject(object_identifier.object_digest(), child, priority),
        ""));
  }
  return batch_->Put(handler, ObjectStatusRow::GetKeyFor(object_status, object_identifier), "");
}

// Object deletion of |object_digest| proceeds in several steps:
// - register the object as pending deletion, and fail if the object already has any live reference.
//   From this point on, if any other part of the code attempts to create an ObjectIdentifier for
//   this object (in particular to read or write it), it will automatically mark the deletion as
//   aborted.
// - collect all the synchronization statuses for the object. A given object may be known under
//   different identifiers, with different sync statuses. Do not decode those object identifiers, as
//   it would create a live reference to the object, that would abort the deletion (see below).
// - for each status, abort if the object is not garbage collectable (ie. has some on-disk
//   references) and batch a delete of the associated keys.
// - batch a delete of the object itself, and all its |references|.
// - store the digest as pending deletion for this batch.
//
// When |Execute| eventually runs, it checks that none of the pending deletions have been aborted,
// ie. that no live references to the object has been introduced since the first step. No on-disk
// reference or change of status can have happened either, because all the entry points in PageDb
// that allow those changes require an ObjectIdentifier as input, the creation of which would have
// aborted the deletion. This is the reason this method is the only one in |PageDb| operating on
// ObjectDigest rather than ObjectIdentifier.
Status PageDbBatchImpl::DeleteObject(coroutine::CoroutineHandler* handler,
                                     const ObjectDigest& object_digest,
                                     const ObjectReferencesAndPriority& references) {
  if (!factory_->TrackDeletion(object_digest)) {
    FXL_VLOG(1) << "Object is live, cannot be deleted: " << object_digest;
    return Status::CANCELED;
  }
  std::map<std::string, PageDbObjectStatus> keys;
  RETURN_ON_ERROR(page_db_->GetObjectStatusKeys(handler, object_digest, &keys));
  // |seen_status| is used to ensure that we call |IsGarbageCollectable| only once per status.
  std::set<PageDbObjectStatus> seen_status;
  for (const auto& [object_status_key, object_status] : keys) {
    if (seen_status.insert(object_status).second) {
      bool is_garbage_collectable = false;
      RETURN_ON_ERROR(
          IsGarbageCollectable(handler, object_digest, object_status, &is_garbage_collectable));
      if (!is_garbage_collectable) {
        // Abort the deletion.
        (void)factory_->UntrackDeletion(object_digest);
        FXL_VLOG(1) << "Object is not garbage collectable, cannot be deleted: " << object_digest;
        return Status::CANCELED;
      }
    }
    RETURN_ON_ERROR(batch_->Delete(handler, object_status_key));
  }
  RETURN_ON_ERROR(batch_->Delete(handler, ObjectRow::GetKeyFor(object_digest)));
  for (const auto& [child, priority] : references) {
    FXL_DCHECK(!GetObjectDigestInfo(child).is_inlined());
    RETURN_ON_ERROR(
        batch_->Delete(handler, ReferenceRow::GetKeyForObject(object_digest, child, priority)));
  }
  pending_deletion_.insert(object_digest);
  return Status::OK;
}

Status PageDbBatchImpl::IsGarbageCollectable(coroutine::CoroutineHandler* handler,
                                           const ObjectDigest& digest,
                                           PageDbObjectStatus object_status,
                                           bool *result) {
  *result = false;
  std::string prefix;
  switch (object_status) {
    case PageDbObjectStatus::UNKNOWN:
      FXL_NOTREACHED();
      return Status::INTERNAL_ERROR;
    case PageDbObjectStatus::SYNCED:
      // object-object links only for synced objects: even if referenced by on-disk commits,
      // it is safe to discard them (and recover them later from the cloud).
      prefix = ReferenceRow::GetObjectKeyPrefixFor(digest);
      break;
    case PageDbObjectStatus::TRANSIENT:
    case PageDbObjectStatus::LOCAL:
      // object-object and commit-object links must both be zero for all other types of objects.
      prefix = ReferenceRow::GetKeyPrefixFor(digest);
      break;
  }
  Status status = db_->HasPrefix(handler, prefix);
  if (status == Status::INTERNAL_NOT_FOUND) {
    *result = true;
    status = Status::OK;
  }
  return status;
}

Status PageDbBatchImpl::SetObjectStatus(CoroutineHandler* handler,
                                        const ObjectIdentifier& object_identifier,
                                        PageDbObjectStatus object_status) {
  FXL_DCHECK(object_status >= PageDbObjectStatus::LOCAL);
  RETURN_ON_ERROR(DCheckHasObject(handler, object_identifier));

  PageDbObjectStatus previous_object_status;
  RETURN_ON_ERROR(page_db_->GetObjectStatus(handler, object_identifier, &previous_object_status));
  if (previous_object_status >= object_status) {
    return Status::OK;
  }
  // The object might exist already under a different identifier (with the same digest), in which
  // case there is no status row to delete.
  if (previous_object_status != PageDbObjectStatus::UNKNOWN) {
    RETURN_ON_ERROR(batch_->Delete(
        handler, ObjectStatusRow::GetKeyFor(previous_object_status, object_identifier)));
  }
  return batch_->Put(handler, ObjectStatusRow::GetKeyFor(object_status, object_identifier), "");
}

Status PageDbBatchImpl::MarkCommitIdSynced(CoroutineHandler* handler, const CommitId& commit_id) {
  return batch_->Delete(handler, UnsyncedCommitRow::GetKeyFor(commit_id));
}

Status PageDbBatchImpl::MarkCommitIdUnsynced(CoroutineHandler* handler, const CommitId& commit_id,
                                             uint64_t generation) {
  return batch_->Put(handler, UnsyncedCommitRow::GetKeyFor(commit_id), SerializeData(generation));
}

Status PageDbBatchImpl::SetSyncMetadata(CoroutineHandler* handler, fxl::StringView key,
                                        fxl::StringView value) {
  return batch_->Put(handler, SyncMetadataRow::GetKeyFor(key), value);
}

Status PageDbBatchImpl::MarkPageOnline(coroutine::CoroutineHandler* handler) {
  return batch_->Put(handler, PageIsOnlineRow::kKey, "");
}

Status PageDbBatchImpl::SetDeviceId(coroutine::CoroutineHandler* handler, DeviceIdView device_id) {
  return batch_->Put(handler, ClockRow::kDeviceIdKey, device_id);
}

Status PageDbBatchImpl::SetClockEntry(coroutine::CoroutineHandler* handler, DeviceIdView device_id,
                                      const ClockEntry& entry) {
  std::string data;
  SerializeClockEntry(entry, &data);
  return batch_->Put(handler, ClockRow::GetClockEntryForKey(device_id), data);
}

Status PageDbBatchImpl::Execute(CoroutineHandler* handler) {
  if (!UntrackPendingDeletions()) {
    return Status::CANCELED;
  }
  return batch_->Execute(handler);
}

bool PageDbBatchImpl::UntrackPendingDeletions() {
  bool aborted = false;
  for (const ObjectDigest& object_digest : pending_deletion_) {
    if (!factory_->UntrackDeletion(object_digest)) {
      FXL_VLOG(1) << "Deletion has been aborted, object cannot be deleted: " << object_digest;
      aborted = true;
    }
  }
  pending_deletion_.clear();
  return !aborted;
}

Status PageDbBatchImpl::DCheckHasObject(CoroutineHandler* handler, const ObjectIdentifier& key) {
#ifdef NDEBUG
  return Status::OK;
#else
  Status status = page_db_->HasObject(handler, key);
  if (status == Status::INTERRUPTED) {
    return status;
  }
  FXL_DCHECK(status == Status::OK) << key;
  return Status::OK;
#endif
}

}  // namespace storage
