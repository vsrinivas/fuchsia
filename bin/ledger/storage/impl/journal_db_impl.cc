// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/journal_db_impl.h"

#include <functional>
#include <string>

#include "apps/ledger/src/storage/impl/btree/btree_utils.h"
#include "apps/ledger/src/storage/impl/commit_impl.h"
#include "apps/ledger/src/storage/impl/db.h"
#include "apps/ledger/src/storage/public/commit.h"
#include "lib/ftl/functional/make_copyable.h"

namespace storage {

JournalDBImpl::JournalDBImpl(JournalType type,
                             PageStorageImpl* page_storage,
                             DB* db,
                             const JournalId& id,
                             const CommitId& base)
    : type_(type),
      page_storage_(page_storage),
      db_(db),
      id_(id),
      base_(base),
      valid_(true),
      failed_operation_(false) {}

JournalDBImpl::~JournalDBImpl() {
  // Log a warning if the journal was not committed or rolled back.
  if (valid_) {
    FTL_LOG(WARNING) << "Journal not committed or rolled back.";
  }
}

std::unique_ptr<Journal> JournalDBImpl::Simple(JournalType type,
                                               PageStorageImpl* page_storage,
                                               DB* db,
                                               const JournalId& id,
                                               const CommitId& base) {
  return std::unique_ptr<Journal>(
      new JournalDBImpl(type, page_storage, db, id, base));
}

std::unique_ptr<Journal> JournalDBImpl::Merge(PageStorageImpl* page_storage,
                                              DB* db,
                                              const JournalId& id,
                                              const CommitId& base,
                                              const CommitId& other) {
  JournalDBImpl* db_journal =
      new JournalDBImpl(JournalType::EXPLICIT, page_storage, db, id, base);
  db_journal->other_ = std::make_unique<CommitId>(other);
  std::unique_ptr<Journal> journal(db_journal);
  return journal;
}

JournalId JournalDBImpl::GetId() const {
  return id_;
}

Status JournalDBImpl::UpdateValueCounter(
    ObjectIdView object_id,
    const std::function<int(int)>& operation) {
  // Update the counter fo untracked objects only.
  if (!page_storage_->ObjectIsUntracked(object_id)) {
    return Status::OK;
  }
  int counter;
  Status s = db_->GetJournalValueCounter(id_, object_id, &counter);
  if (s != Status::OK) {
    return s;
  }
  int next_counter = operation(counter);
  FTL_DCHECK(next_counter >= 0);
  s = db_->SetJournalValueCounter(id_, object_id, next_counter);
  return s;
}

Status JournalDBImpl::Put(convert::ExtendedStringView key,
                          ObjectIdView object_id,
                          KeyPriority priority) {
  if (!valid_ || (type_ == JournalType::EXPLICIT && failed_operation_)) {
    return Status::ILLEGAL_STATE;
  }
  std::string prev_id;
  Status prev_entry_status = db_->GetJournalValue(id_, key, &prev_id);

  std::unique_ptr<DB::Batch> batch = db_->StartBatch();
  Status s = db_->AddJournalEntry(id_, key, object_id, priority);
  if (s != Status::OK) {
    failed_operation_ = true;
    return s;
  }
  UpdateValueCounter(object_id, [](int counter) { return counter + 1; });
  if (prev_entry_status == Status::OK) {
    UpdateValueCounter(prev_id, [](int counter) { return counter - 1; });
  }
  return batch->Execute();
}

Status JournalDBImpl::Delete(convert::ExtendedStringView key) {
  if (!valid_ || (type_ == JournalType::EXPLICIT && failed_operation_)) {
    return Status::ILLEGAL_STATE;
  }
  std::string prev_id;
  Status prev_entry_status = db_->GetJournalValue(id_, key, &prev_id);

  std::unique_ptr<DB::Batch> batch = db_->StartBatch();
  Status s = db_->RemoveJournalEntry(id_, key);
  if (s != Status::OK) {
    failed_operation_ = true;
    return s;
  }

  if (prev_entry_status == Status::OK) {
    UpdateValueCounter(prev_id, [](int counter) { return counter - 1; });
  }
  return batch->Execute();
}

void JournalDBImpl::Commit(
    std::function<void(Status, const CommitId&)> callback) {
  if (!valid_ || (type_ == JournalType::EXPLICIT && failed_operation_)) {
    callback(Status::ILLEGAL_STATE, "");
    return;
  }
  std::unique_ptr<Iterator<const EntryChange>> entries;
  Status status = db_->GetJournalEntries(id_, &entries);
  if (status != Status::OK) {
    callback(status, "");
    return;
  }

  std::unique_ptr<const storage::Commit> base_commit;
  status = page_storage_->GetCommit(base_, &base_commit);
  if (status != Status::OK) {
    callback(status, "");
    return;
  }

  size_t node_size;
  status = db_->GetNodeSize(&node_size);
  if (status != Status::OK) {
    callback(status, "");
    return;
  }

  btree::ApplyChanges(
      page_storage_, base_commit->GetRootId(), node_size, std::move(entries),
      [this, callback](Status status, ObjectId object_id,
                       std::unordered_set<ObjectId>&& new_nodes) {
        if (status != Status::OK) {
          callback(status, "");
          return;
        }

        std::vector<CommitId> parents({base_});
        if (other_) {
          parents.push_back(*other_);
        }

        std::unique_ptr<storage::Commit> commit =
            CommitImpl::FromContentAndParents(page_storage_, object_id,
                                              std::move(parents));
        ObjectId id = commit->GetId();

        page_storage_->AddCommitFromLocal(
            std::move(commit), ftl::MakeCopyable([
              this, id = std::move(id), new_nodes = std::move(new_nodes),
              callback
            ](Status status) mutable {
              valid_ = false;
              if (status != Status::OK) {
                callback(status, "");
                return;
              }
              // Mark objects as unsynced.
              std::vector<ObjectId> objects_to_sync;
              status = db_->GetJournalValues(id_, &objects_to_sync);
              if (status != Status::OK) {
                callback(status, "");
                return;
              }
              // Mark unsynced objects in a single batch.
              std::unique_ptr<DB::Batch> batch = db_->StartBatch();
              for (const ObjectId& tree_node_id : new_nodes) {
                status = db_->MarkObjectIdUnsynced(tree_node_id);
                if (status != Status::OK) {
                  callback(status, "");
                  return;
                }
              }
              for (const ObjectId& object_id : objects_to_sync) {
                status = db_->MarkObjectIdUnsynced(object_id);
                if (status != Status::OK) {
                  callback(status, "");
                  return;
                }
              }
              status = batch->Execute();
              if (status != Status::OK) {
                callback(status, "");
                return;
              }
              // Notify PageStorage that the objects are now tracked.
              for (const ObjectId& object_id : objects_to_sync) {
                page_storage_->MarkObjectTracked(object_id);
              }
              db_->RemoveJournal(id_);
              callback(Status::OK, id);
            }));
      });
}

Status JournalDBImpl::Rollback() {
  if (!valid_) {
    return Status::ILLEGAL_STATE;
  }
  Status s = db_->RemoveJournal(id_);
  if (s == Status::OK) {
    valid_ = false;
  }
  return s;
}

}  // namespace storage
