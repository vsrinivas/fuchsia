// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/journal_impl.h"

#include <functional>
#include <map>
#include <string>
#include <utility>

#include <lib/callback/waiter.h>
#include <lib/fit/function.h>
#include <lib/fxl/memory/ref_ptr.h>

#include "peridot/bin/ledger/storage/impl/btree/builder.h"
#include "peridot/bin/ledger/storage/impl/commit_impl.h"
#include "peridot/bin/ledger/storage/public/commit.h"

namespace storage {

JournalImpl::JournalImpl(Token /* token */, JournalType type,
                         coroutine::CoroutineService* coroutine_service,
                         PageStorageImpl* page_storage, JournalId id,
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
    JournalType type, coroutine::CoroutineService* coroutine_service,
    PageStorageImpl* page_storage, const JournalId& id, const CommitId& base) {
  return std::make_unique<JournalImpl>(Token(), type, coroutine_service,
                                       page_storage, id, base);
}

std::unique_ptr<Journal> JournalImpl::Merge(
    coroutine::CoroutineService* coroutine_service,
    PageStorageImpl* page_storage, const JournalId& id, const CommitId& base,
    const CommitId& other) {
  auto journal =
      std::make_unique<JournalImpl>(Token(), JournalType::EXPLICIT,
                                    coroutine_service, page_storage, id, base);
  journal->other_ = std::make_unique<CommitId>(other);
  return journal;
}

const JournalId& JournalImpl::GetId() const { return id_; }

void JournalImpl::Commit(
    fit::function<void(Status, std::unique_ptr<const storage::Commit>)>
        callback) {
  serializer_.Serialize<Status, std::unique_ptr<const storage::Commit>>(
      std::move(callback),
      [this](fit::function<void(Status, std::unique_ptr<const storage::Commit>)>
                 callback) {
        if (!valid_ || (type_ == JournalType::EXPLICIT && failed_operation_)) {
          callback(Status::ILLEGAL_STATE, nullptr);
          return;
        }

        GetParents([this, callback = std::move(callback)](
                       Status status,
                       std::vector<std::unique_ptr<const storage::Commit>>
                           parents) mutable {
          if (status != Status::OK) {
            callback(status, nullptr);
            return;
          }
          page_storage_->GetJournalEntries(
              id_,
              [this, parents = std::move(parents),
               callback = std::move(callback)](
                  Status status, std::unique_ptr<Iterator<const EntryChange>>
                                     changes) mutable {
                if (status != Status::OK) {
                  callback(status, nullptr);
                  return;
                }
                CreateCommitFromChanges(std::move(parents), std::move(changes),
                                        std::move(callback));
              });
        });
      });
}

void JournalImpl::Rollback(fit::function<void(Status)> callback) {
  serializer_.Serialize<Status>(std::move(callback),
                                [this](fit::function<void(Status)> callback) {
                                  RollbackInternal(std::move(callback));
                                });
}

void JournalImpl::Put(convert::ExtendedStringView key,
                      ObjectIdentifier object_identifier, KeyPriority priority,
                      fit::function<void(Status)> callback) {
  serializer_.Serialize<Status>(
      std::move(callback),
      [this, key = key.ToString(),
       object_identifier = std::move(object_identifier),
       priority](fit::function<void(Status)> callback) mutable {
        if (!valid_ || (type_ == JournalType::EXPLICIT && failed_operation_)) {
          callback(Status::ILLEGAL_STATE);
          return;
        }
        page_storage_->AddJournalEntry(
            id_, key, std::move(object_identifier), priority,
            [this, callback = std::move(callback)](Status s) {
              if (s != Status::OK) {
                failed_operation_ = true;
              }
              callback(s);
            });
      });
}

void JournalImpl::Delete(convert::ExtendedStringView key,
                         fit::function<void(Status)> callback) {
  serializer_.Serialize<Status>(
      std::move(callback),
      [this, key = key.ToString()](fit::function<void(Status)> callback) {
        if (!valid_ || (type_ == JournalType::EXPLICIT && failed_operation_)) {
          callback(Status::ILLEGAL_STATE);
          return;
        }

        page_storage_->RemoveJournalEntry(
            id_, key, [this, callback = std::move(callback)](Status s) {
              if (s != Status::OK) {
                failed_operation_ = true;
              }
              callback(s);
            });
      });
}

void JournalImpl::GetParents(
    fit::function<void(Status,
                       std::vector<std::unique_ptr<const storage::Commit>>)>
        callback) {
  auto waiter = fxl::MakeRefCounted<
      callback::Waiter<Status, std::unique_ptr<const storage::Commit>>>(
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
    fit::function<void(Status, std::unique_ptr<const storage::Commit>)>
        callback) {
  btree::ApplyChanges(
      coroutine_service_, page_storage_, parents[0]->GetRootIdentifier(),
      std::move(changes),
      [this, parents = std::move(parents), callback = std::move(callback)](
          Status status, ObjectIdentifier object_identifier,
          std::set<ObjectIdentifier> new_nodes) mutable {
        if (status != Status::OK) {
          callback(status, nullptr);
          return;
        }
        // If the commit is a no-op, return early.
        if (parents.size() == 1 &&
            parents.front()->GetRootIdentifier() == object_identifier) {
          FXL_DCHECK(new_nodes.empty());
          // We are in an operation from the serializer: make sure not to sent
          // the rollback operation in the serializer as well, or a deadlock
          // will be created.
          RollbackInternal(
              [parent = std::move(parents.front()),
               callback = std::move(callback)](Status status) mutable {
                callback(status, std::move(parent));
              });
          return;
        }
        std::unique_ptr<const storage::Commit> commit =
            CommitImpl::FromContentAndParents(page_storage_, object_identifier,
                                              std::move(parents));
        GetObjectsToSync([this, new_nodes = std::move(new_nodes),
                          commit = std::move(commit),
                          callback = std::move(callback)](
                             Status status, std::vector<ObjectIdentifier>
                                                objects_to_sync) mutable {
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
              commit->Clone(), std::move(objects_to_sync),
              [this, commit = std::move(commit),
               callback = std::move(callback)](Status status) mutable {
                valid_ = false;
                if (status != Status::OK) {
                  callback(status, nullptr);
                  return;
                }
                page_storage_->RemoveJournal(
                    id_,
                    [commit = std::move(commit),
                     callback = std::move(callback)](Status status) mutable {
                      if (status != Status::OK) {
                        FXL_LOG(INFO)
                            << "Commit created, but failed to delete journal.";
                      }
                      callback(Status::OK, std::move(commit));
                    });
              });
        });
      });
}

void JournalImpl::GetObjectsToSync(
    fit::function<void(Status status,
                       std::vector<ObjectIdentifier> objects_to_sync)>
        callback) {
  page_storage_->GetJournalEntries(
      id_, [this, callback = std::move(callback)](
               Status s,
               std::unique_ptr<Iterator<const EntryChange>> entries) mutable {
        if (s != Status::OK) {
          callback(s, {});
          return;
        }
        // Compute the key-value pairs added in this journal.
        std::map<std::string, ObjectIdentifier> key_values;
        while (entries->Valid()) {
          const Entry& entry = (*entries)->entry;
          if ((*entries)->deleted) {
            key_values.erase(entry.key);
          } else {
            key_values[entry.key] = entry.object_identifier;
          }
          entries->Next();
        }
        auto waiter =
            fxl::MakeRefCounted<callback::Waiter<Status, bool>>(Status::OK);
        for (const auto& key_value : key_values) {
          page_storage_->ObjectIsUntracked(key_value.second,
                                           waiter->NewCallback());
        }
        waiter->Finalize([key_values = std::move(key_values),
                          callback = std::move(callback)](
                             Status s, std::vector<bool> is_untracked) {
          if (s != Status::OK) {
            callback(s, {});
            return;
          }
          // Compute the set of values.
          std::set<ObjectIdentifier> result_set;
          size_t i = 0;
          for (const auto& key_value : key_values) {
            // Only untracked objects should be synced.
            if (is_untracked[i++]) {
              result_set.insert(key_value.second);
            }
          }
          std::vector<ObjectIdentifier> objects_to_sync;
          std::copy(result_set.begin(), result_set.end(),
                    std::back_inserter(objects_to_sync));
          callback(Status::OK, std::move(objects_to_sync));
        });
      });
}

void JournalImpl::RollbackInternal(fit::function<void(Status)> callback) {
  if (!valid_) {
    callback(Status::ILLEGAL_STATE);
    return;
  }
  page_storage_->RemoveJournal(
      id_, [this, callback = std::move(callback)](Status s) {
        if (s == Status::OK) {
          valid_ = false;
        }
        callback(s);
      });
}

}  // namespace storage
