// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/page_db_batch_impl.h"

#include <memory>

#include "lib/fxl/strings/concatenate.h"
#include "peridot/bin/ledger/storage/impl/db_serialization.h"
#include "peridot/bin/ledger/storage/impl/journal_impl.h"
#include "peridot/bin/ledger/storage/impl/number_serialization.h"

#define RETURN_ON_ERROR(expr)   \
  do {                          \
    Status status = (expr);     \
    if (status != Status::OK) { \
      return status;            \
    }                           \
  } while (0)

namespace storage {

using coroutine::CoroutineHandler;

PageDbBatchImpl::PageDbBatchImpl(std::unique_ptr<Db::Batch> batch, PageDb* db)
    : batch_(std::move(batch)), db_(db) {}

PageDbBatchImpl::~PageDbBatchImpl() {}

Status PageDbBatchImpl::AddHead(CoroutineHandler* handler,
                                CommitIdView head,
                                int64_t timestamp) {
  return batch_->Put(handler, HeadRow::GetKeyFor(head),
                     SerializeNumber(timestamp));
}

Status PageDbBatchImpl::RemoveHead(CoroutineHandler* handler,
                                   CommitIdView head) {
  return batch_->Delete(handler, HeadRow::GetKeyFor(head));
}

Status PageDbBatchImpl::AddCommitStorageBytes(CoroutineHandler* handler,
                                              const CommitId& commit_id,
                                              fxl::StringView storage_bytes) {
  return batch_->Put(handler, CommitRow::GetKeyFor(commit_id), storage_bytes);
}

Status PageDbBatchImpl::RemoveCommit(CoroutineHandler* handler,
                                     const CommitId& commit_id) {
  return batch_->Delete(handler, CommitRow::GetKeyFor(commit_id));
}

Status PageDbBatchImpl::CreateJournalId(coroutine::CoroutineHandler* handler,
                                        JournalType journal_type,
                                        const CommitId& base,
                                        JournalId* journal_id) {
  JournalId id = JournalEntryRow::NewJournalId(journal_type);

  Status status = Status::OK;
  if (journal_type == JournalType::IMPLICIT) {
    status = batch_->Put(handler, ImplicitJournalMetaRow::GetKeyFor(id), base);
  }

  if (status == Status::OK) {
    journal_id->swap(id);
  }
  return status;
}

Status PageDbBatchImpl::RemoveExplicitJournals(CoroutineHandler* handler) {
  static std::string kExplicitJournalPrefix =
      fxl::Concatenate({JournalEntryRow::kPrefix,
                        fxl::StringView(&JournalEntryRow::kImplicitPrefix, 1)});
  return batch_->DeleteByPrefix(handler, kExplicitJournalPrefix);
}

Status PageDbBatchImpl::RemoveJournal(CoroutineHandler* handler,
                                      const JournalId& journal_id) {
  if (journal_id[0] == JournalEntryRow::kImplicitPrefix) {
    RETURN_ON_ERROR(
        batch_->Delete(handler, ImplicitJournalMetaRow::GetKeyFor(journal_id)));
  }
  return batch_->DeleteByPrefix(handler,
                                JournalEntryRow::GetPrefixFor(journal_id));
}

Status PageDbBatchImpl::AddJournalEntry(coroutine::CoroutineHandler* handler,
                                        const JournalId& journal_id,
                                        fxl::StringView key,
                                        fxl::StringView value,
                                        KeyPriority priority) {
  return batch_->Put(handler, JournalEntryRow::GetKeyFor(journal_id, key),
                     JournalEntryRow::GetValueFor(value, priority));
}

Status PageDbBatchImpl::RemoveJournalEntry(coroutine::CoroutineHandler* handler,
                                           const JournalId& journal_id,
                                           convert::ExtendedStringView key) {
  return batch_->Put(handler, JournalEntryRow::GetKeyFor(journal_id, key),
                     JournalEntryRow::kDeletePrefix);
}

Status PageDbBatchImpl::WriteObject(
    CoroutineHandler* handler,
    ObjectIdView object_id,
    std::unique_ptr<DataSource::DataChunk> content,
    PageDbObjectStatus object_status) {
  FXL_DCHECK(object_status > PageDbObjectStatus::UNKNOWN);

  auto object_key = ObjectRow::GetKeyFor(object_id);
  bool has_key;
  RETURN_ON_ERROR(db_->HasObject(handler, object_id, &has_key));
  if (has_key && object_status > PageDbObjectStatus::TRANSIENT) {
    return SetObjectStatus(handler, object_id, object_status);
  }

  RETURN_ON_ERROR(batch_->Put(handler, object_key, content->Get()));
  switch (object_status) {
    case PageDbObjectStatus::UNKNOWN:
      FXL_NOTREACHED();
      break;
    case PageDbObjectStatus::TRANSIENT:
      return batch_->Put(handler, TransientObjectRow::GetKeyFor(object_id), "");
    case PageDbObjectStatus::LOCAL:
      return batch_->Put(handler, LocalObjectRow::GetKeyFor(object_id), "");
    case PageDbObjectStatus::SYNCED:
      // Nothing to do.
      break;
  }
  return Status::OK;
}

Status PageDbBatchImpl::DeleteObject(CoroutineHandler* handler,
                                     ObjectIdView object_id) {
  RETURN_ON_ERROR(batch_->Delete(handler, ObjectRow::GetKeyFor(object_id)));
  RETURN_ON_ERROR(
      batch_->Delete(handler, TransientObjectRow::GetKeyFor(object_id)));
  return batch_->Delete(handler, LocalObjectRow::GetKeyFor(object_id));
}

Status PageDbBatchImpl::SetObjectStatus(CoroutineHandler* handler,
                                        ObjectIdView object_id,
                                        PageDbObjectStatus object_status) {
  FXL_DCHECK(object_status >= PageDbObjectStatus::LOCAL);
  FXL_DCHECK(CheckHasObject(handler, object_id))
      << "Unknown object: " << convert::ToHex(object_id);

  auto transient_key = TransientObjectRow::GetKeyFor(object_id);
  auto local_key = LocalObjectRow::GetKeyFor(object_id);

  switch (object_status) {
    case PageDbObjectStatus::UNKNOWN:
    case PageDbObjectStatus::TRANSIENT: {
      FXL_NOTREACHED();
      break;
    }
    case PageDbObjectStatus::LOCAL: {
      PageDbObjectStatus previous_object_status;
      RETURN_ON_ERROR(
          db_->GetObjectStatus(handler, object_id, &previous_object_status));
      if (previous_object_status == PageDbObjectStatus::TRANSIENT) {
        RETURN_ON_ERROR(batch_->Delete(handler, transient_key));
        return batch_->Put(handler, local_key, "");
      }
      break;
    }
    case PageDbObjectStatus::SYNCED: {
      RETURN_ON_ERROR(batch_->Delete(handler, local_key));
      return batch_->Delete(handler, transient_key);
    }
  }

  return Status::OK;
}

Status PageDbBatchImpl::MarkCommitIdSynced(CoroutineHandler* handler,
                                           const CommitId& commit_id) {
  return batch_->Delete(handler, UnsyncedCommitRow::GetKeyFor(commit_id));
}

Status PageDbBatchImpl::MarkCommitIdUnsynced(CoroutineHandler* handler,
                                             const CommitId& commit_id,
                                             uint64_t generation) {
  return batch_->Put(handler, UnsyncedCommitRow::GetKeyFor(commit_id),
                     SerializeNumber(generation));
}

Status PageDbBatchImpl::SetSyncMetadata(CoroutineHandler* handler,
                                        fxl::StringView key,
                                        fxl::StringView value) {
  return batch_->Put(handler, SyncMetadataRow::GetKeyFor(key), value);
}

Status PageDbBatchImpl::Execute(CoroutineHandler* handler) {
  return batch_->Execute(handler);
}

bool PageDbBatchImpl::CheckHasObject(CoroutineHandler* handler,
                                     convert::ExtendedStringView key) {
  bool result;
  Status status = db_->HasObject(handler, key, &result);
  if (status != Status::OK) {
    return false;
  }
  return result;
}

}  // namespace storage
