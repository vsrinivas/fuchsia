// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/journal_impl.h"

#include <functional>
#include <map>
#include <string>
#include <utility>

#include "apps/ledger/src/callback/waiter.h"
#include "apps/ledger/src/storage/impl/btree/builder.h"
#include "apps/ledger/src/storage/impl/commit_impl.h"
#include "apps/ledger/src/storage/public/commit.h"
#include "lib/fxl/functional/make_copyable.h"

namespace storage {

JournalImpl::JournalImpl(JournalType type,
                         coroutine::CoroutineService* coroutine_service,
                         PageStorageImpl* page_storage,
                         JournalId id,
                         CommitId base)
    : type_(type),
      coroutine_service_(coroutine_service),
      page_storage_(page_storage),
      id_(std::move(id)),
      base_(std::move(base)),
      valid_(true),
      failed_operation_(false) {}

JournalImpl::~JournalImpl() {
  // Log a warning if the journal was not committed or rolled back.
  if (valid_) {
    FXL_LOG(WARNING) << "Journal not committed or rolled back.";
  }
}

std::unique_ptr<Journal> JournalImpl::Simple(
    JournalType type,
    coroutine::CoroutineService* coroutine_service,
    PageStorageImpl* page_storage,
    const JournalId& id,
    const CommitId& base) {
  return std::unique_ptr<Journal>(
      new JournalImpl(type, coroutine_service, page_storage, id, base));
}

std::unique_ptr<Journal> JournalImpl::Merge(
    coroutine::CoroutineService* coroutine_service,
    PageStorageImpl* page_storage,
    const JournalId& id,
    const CommitId& base,
    const CommitId& other) {
  JournalImpl* db_journal = new JournalImpl(
      JournalType::EXPLICIT, coroutine_service, page_storage, id, base);
  db_journal->other_ = std::make_unique<CommitId>(other);
  std::unique_ptr<Journal> journal(db_journal);
  return journal;
}

const JournalId& JournalImpl::GetId() const {
  return id_;
}

void JournalImpl::Commit(
    std::function<void(Status, std::unique_ptr<const storage::Commit>)>
        callback) {
  serializer_.Serialize<Status, std::unique_ptr<const storage::Commit>>(
      std::move(callback),
      [this](std::function<void(Status, std::unique_ptr<const storage::Commit>)>
                 callback) {
        if (!valid_ || (type_ == JournalType::EXPLICIT && failed_operation_)) {
          callback(Status::ILLEGAL_STATE, nullptr);
          return;
        }

        GetParents([
          this, callback = std::move(callback)
        ](Status status,
          std::vector<std::unique_ptr<const storage::Commit>> parents) mutable {
          if (status != Status::OK) {
            callback(status, nullptr);
            return;
          }
          page_storage_->GetJournalEntries(
              id_,
              fxl::MakeCopyable([
                this, parents = std::move(parents),
                callback = std::move(callback)
              ](Status status,
                std::unique_ptr<Iterator<const EntryChange>> changes) mutable {
                if (status != Status::OK) {
                  callback(status, nullptr);
                  return;
                }
                CreateCommitFromChanges(std::move(parents), std::move(changes),
                                        std::move(callback));
              }));
        });
      });
}

void JournalImpl::Rollback(std::function<void(Status)> callback) {
  serializer_.Serialize<Status>(std::move(callback),
                                [this](std::function<void(Status)> callback) {
                                  RollbackInternal(std::move(callback));
                                });
}

void JournalImpl::Put(convert::ExtendedStringView key,
                      ObjectIdView object_id,
                      KeyPriority priority,
                      std::function<void(Status)> callback) {
  serializer_.Serialize<Status>(std::move(callback), [
    this, key = key.ToString(), object_id = object_id.ToString(), priority
  ](std::function<void(Status)> callback) {
    if (!valid_ || (type_ == JournalType::EXPLICIT && failed_operation_)) {
      callback(Status::ILLEGAL_STATE);
      return;
    }
    page_storage_->AddJournalEntry(
        id_, key, object_id, priority,
        [ this, callback = std::move(callback) ](Status s) {
          if (s != Status::OK) {
            failed_operation_ = true;
          }
          callback(s);
        });
  });
}

void JournalImpl::Delete(convert::ExtendedStringView key,
                         std::function<void(Status)> callback) {
  serializer_.Serialize<Status>(
      std::move(callback),
      [ this, key = key.ToString() ](std::function<void(Status)> callback) {
        if (!valid_ || (type_ == JournalType::EXPLICIT && failed_operation_)) {
          callback(Status::ILLEGAL_STATE);
          return;
        }

        page_storage_->RemoveJournalEntry(
            id_, key, [ this, callback = std::move(callback) ](Status s) {
              if (s != Status::OK) {
                failed_operation_ = true;
              }
              callback(s);
            });
      });
}

void JournalImpl::GetParents(
    std::function<void(Status,
                       std::vector<std::unique_ptr<const storage::Commit>>)>
        callback) {
  auto waiter =
      callback::Waiter<Status, std::unique_ptr<const storage::Commit>>::Create(
          Status::OK);
  page_storage_->GetCommit(base_, waiter->NewCallback());
  if (other_) {
    page_storage_->GetCommit(*other_, waiter->NewCallback());
  }
  waiter->Finalize(std::move(callback));
}

void JournalImpl::CreateCommitFromChanges(
    std::vector<std::unique_ptr<const storage::Commit>> parents,
    std::unique_ptr<Iterator<const EntryChange>> changes,
    std::function<void(Status, std::unique_ptr<const storage::Commit>)>
        callback) {
  btree::ApplyChanges(
      coroutine_service_, page_storage_, parents[0]->GetRootId(),
      std::move(changes),
      fxl::MakeCopyable([
        this, parents = std::move(parents), callback = std::move(callback)
      ](Status status, ObjectId object_id,
        std::unordered_set<ObjectId> new_nodes) mutable {
        if (status != Status::OK) {
          callback(status, nullptr);
          return;
        }
        // If the commit is a no-op, return early.
        if (parents.size() == 1 && parents.front()->GetRootId() == object_id) {
          FXL_DCHECK(new_nodes.empty());
          // We are in an operation from the serializer: make sure not to sent
          // the rollback operation in the serializer as well, or a deadlock
          // will be created.
          RollbackInternal(fxl::MakeCopyable([
            parent = std::move(parents.front()), callback = std::move(callback)
          ](Status status) mutable { callback(status, std::move(parent)); }));
          return;
        }
        std::unique_ptr<storage::Commit> commit =
            CommitImpl::FromContentAndParents(page_storage_, object_id,
                                              std::move(parents));
        GetObjectsToSync(fxl::MakeCopyable([
          this, new_nodes = std::move(new_nodes), commit = std::move(commit),
          callback = std::move(callback)
        ](Status status, std::vector<ObjectId> objects_to_sync) mutable {
          if (status != Status::OK) {
            callback(status, nullptr);
            return;
          }

          objects_to_sync.reserve(objects_to_sync.size() + new_nodes.size());
          // TODO(qsr): When using C++17, move data out of the set using
          // extract.
          objects_to_sync.insert(objects_to_sync.end(), new_nodes.begin(),
                                 new_nodes.end());
          page_storage_->AddCommitFromLocal(
              commit->Clone(), std::move(objects_to_sync), fxl::MakeCopyable([
                this, commit = std::move(commit), callback = std::move(callback)
              ](Status status) mutable {
                valid_ = false;
                if (status != Status::OK) {
                  callback(status, nullptr);
                  return;
                }
                page_storage_->RemoveJournal(
                    id_, fxl::MakeCopyable([
                      commit = std::move(commit), callback = std::move(callback)
                    ](Status status) mutable {
                      if (status != Status::OK) {
                        FXL_LOG(INFO)
                            << "Commit created, but failed to delete journal.";
                      }
                      callback(Status::OK, std::move(commit));
                    }));
              }));
        }));
      }));
}

void JournalImpl::GetObjectsToSync(
    std::function<void(Status status, std::vector<ObjectId> objects_to_sync)>
        callback) {
  page_storage_->GetJournalEntries(
      id_, fxl::MakeCopyable([ this, callback = std::move(callback) ](
               Status s, std::unique_ptr<Iterator<const EntryChange>> entries) {
        if (s != Status::OK) {
          callback(s, {});
          return;
        }
        // Compute the key-value pairs added in this journal.
        std::map<std::string, ObjectId> key_values;
        while (entries->Valid()) {
          const Entry& entry = (*entries)->entry;
          if ((*entries)->deleted) {
            key_values.erase(entry.key);
          } else {
            key_values[entry.key] = entry.object_id;
          }
          entries->Next();
        }
        auto waiter = callback::Waiter<Status, bool>::Create(Status::OK);
        for (const auto& key_value : key_values) {
          page_storage_->ObjectIsUntracked(key_value.second,
                                           waiter->NewCallback());
        }
        waiter->Finalize([
          key_values = std::move(key_values), callback = std::move(callback)
        ](Status s, std::vector<bool> is_untracked) {
          if (s != Status::OK) {
            callback(s, {});
            return;
          }
          // Compute the set of values.
          std::set<ObjectId> result_set;
          size_t i = 0;
          for (const auto& key_value : key_values) {
            // Only untracked objects should be synced.
            if (is_untracked[i++]) {
              result_set.insert(key_value.second);
            }
          }
          std::vector<ObjectId> objects_to_sync;
          std::copy(result_set.begin(), result_set.end(),
                    std::back_inserter(objects_to_sync));
          callback(Status::OK, std::move(objects_to_sync));
        });
      }));
}

void JournalImpl::RollbackInternal(std::function<void(Status)> callback) {
  if (!valid_) {
    callback(Status::ILLEGAL_STATE);
    return;
  }
  page_storage_->RemoveJournal(
      id_, [ this, callback = std::move(callback) ](Status s) {
        if (s == Status::OK) {
          valid_ = false;
        }
        callback(s);
      });
}

}  // namespace storage
