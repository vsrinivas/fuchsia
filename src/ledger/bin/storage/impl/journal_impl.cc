// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/journal_impl.h"

#include <lib/fit/function.h>

#include <functional>
#include <map>
#include <string>
#include <utility>

#include "src/ledger/bin/storage/impl/btree/builder.h"
#include "src/ledger/bin/storage/impl/btree/encoding.h"
#include "src/ledger/bin/storage/impl/btree/tree_node.h"
#include "src/ledger/bin/storage/impl/commit_factory.h"
#include "src/ledger/bin/storage/impl/data_serialization.h"
#include "src/ledger/bin/storage/impl/object_identifier_encoding.h"
#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/callback/waiter.h"
#include "src/lib/fxl/memory/ref_ptr.h"

namespace storage {

JournalImpl::JournalImpl(Token /* token */, ledger::Environment* environment,
                         PageStorageImpl* page_storage, std::unique_ptr<const storage::Commit> base)
    : environment_(environment),
      page_storage_(page_storage),
      base_(std::move(base)),
      committed_(false) {}

JournalImpl::~JournalImpl() = default;

std::unique_ptr<Journal> JournalImpl::Simple(ledger::Environment* environment,
                                             PageStorageImpl* page_storage,
                                             std::unique_ptr<const storage::Commit> base) {
  FXL_DCHECK(base);

  return std::make_unique<JournalImpl>(Token(), environment, page_storage, std::move(base));
}

std::unique_ptr<Journal> JournalImpl::Merge(ledger::Environment* environment,
                                            PageStorageImpl* page_storage,
                                            std::unique_ptr<const storage::Commit> base,
                                            std::unique_ptr<const storage::Commit> other) {
  FXL_DCHECK(base);
  FXL_DCHECK(other);
  auto journal = std::make_unique<JournalImpl>(Token(), environment, page_storage, std::move(base));
  journal->other_ = std::move(other);
  return journal;
}

Status JournalImpl::Commit(coroutine::CoroutineHandler* handler,
                           std::unique_ptr<const storage::Commit>* commit,
                           std::vector<ObjectIdentifier>* objects_to_sync) {
  FXL_DCHECK(!committed_);
  committed_ = true;
  objects_to_sync->clear();

  std::vector<storage::EntryChange> changes;
  for (auto [key, entry_change] : journal_entries_) {
    changes.push_back(std::move(entry_change));
  }
  SetEntryIds(&changes);

  // |base_| and |other_| must be cloned, because they keep the potential bases for diffs alive, and
  // |parents| will be discarded before this JournalImpl is destructed.
  std::vector<std::unique_ptr<const storage::Commit>> parents;
  if (other_) {
    parents.reserve(2);
    parents.push_back(base_->Clone());
    parents.push_back(other_->Clone());
  } else {
    parents.reserve(1);
    parents.push_back(base_->Clone());
  }

  if (cleared_ == JournalContainsClearOperation::NO) {
    // The journal doesn't contain the clear operation. The changes
    // recorded on the journal need to be executed over the content of
    // the first parent.
    ObjectIdentifier root_identifier = parents[0]->GetRootIdentifier();
    std::string parent_id = parents[0]->GetId();
    return CreateCommitFromChanges(
        handler, std::move(parents),
        {std::move(root_identifier), PageStorage::Location::TreeNodeFromNetwork(parent_id)},
        std::move(changes), commit, objects_to_sync);
  }

  // The journal contains the clear operation. The changes recorded on the
  // journal need to be executed over an empty page.
  Status status;
  ObjectIdentifier root_identifier;
  if (coroutine::SyncCall(
          handler,
          [this](fit::function<void(Status status, ObjectIdentifier root_identifier)> callback) {
            btree::TreeNode::Empty(page_storage_, std::move(callback));
          },
          &status, &root_identifier) == coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERRUPTED;
  }
  RETURN_ON_ERROR(status);
  return CreateCommitFromChanges(handler, std::move(parents),
                                 {std::move(root_identifier), PageStorage::Location::Local()},
                                 std::move(changes), commit, objects_to_sync);
}

void JournalImpl::Put(convert::ExtendedStringView key, ObjectIdentifier object_identifier,
                      KeyPriority priority) {
  FXL_DCHECK(!committed_);
  EntryChange change;
  change.entry = {convert::ToString(key), std::move(object_identifier), priority, EntryId()};
  change.deleted = false;
  journal_entries_[convert::ToString(key)] = std::move(change);
}

void JournalImpl::Delete(convert::ExtendedStringView key) {
  FXL_DCHECK(!committed_);
  EntryChange change;
  change.entry = {convert::ToString(key), ObjectIdentifier(), KeyPriority::EAGER, EntryId()};
  change.deleted = true;
  journal_entries_[convert::ToString(key)] = std::move(change);
}

void JournalImpl::Clear() {
  FXL_DCHECK(!committed_);
  cleared_ = JournalContainsClearOperation::YES;
  journal_entries_.clear();
}

Status JournalImpl::CreateCommitFromChanges(
    coroutine::CoroutineHandler* handler,
    std::vector<std::unique_ptr<const storage::Commit>> parents,
    btree::LocatedObjectIdentifier root_identifier, std::vector<EntryChange> changes,
    std::unique_ptr<const storage::Commit>* commit,
    std::vector<ObjectIdentifier>* objects_to_sync) {
  ObjectIdentifier object_identifier;
  std::set<ObjectIdentifier> new_nodes;
  RETURN_ON_ERROR(btree::ApplyChanges(handler, page_storage_, std::move(root_identifier),
                                      std::move(changes), &object_identifier, &new_nodes));
  // If the commit is a no-op, return early, without creating a new
  // commit.
  if (parents.size() == 1 && parents.front()->GetRootIdentifier() == object_identifier) {
    // |new_nodes| can be ignored here. If a clear operation has been
    // executed and the state has then been restored to the one before the
    // transaction, |ApplyChanges| might have re-created some nodes that
    // already exist. Because they already exist in a pre-existing commit,
    // there is no need to update their state.
    *commit = nullptr;
    return Status::OK;
  }

  std::unique_ptr<const storage::Commit> new_commit =
      page_storage_->GetCommitFactory()->FromContentAndParents(
          environment_->clock(), environment_->random(), object_identifier, std::move(parents));

  Status status;
  if (coroutine::SyncCall(
          handler,
          [this](fit::function<void(Status, std::vector<ObjectIdentifier>)> callback) {
            GetObjectsToSync(std::move(callback));
          },
          &status, objects_to_sync) == coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERRUPTED;
  }
  RETURN_ON_ERROR(status);

  // TODO(12356): remove compatibility flag.
  if (environment_->diff_compatibility_policy() ==
      DiffCompatibilityPolicy::USE_DIFFS_AND_TREE_NODES) {
    objects_to_sync->reserve(objects_to_sync->size() + new_nodes.size());
    // TODO(qsr): When using C++17, move data out of the set using
    // extract.
    objects_to_sync->insert(objects_to_sync->end(), new_nodes.begin(), new_nodes.end());
  }

  *commit = std::move(new_commit);
  return Status::OK;
}

void JournalImpl::GetObjectsToSync(
    fit::function<void(Status status, std::vector<ObjectIdentifier> objects_to_sync)> callback) {
  auto waiter = fxl::MakeRefCounted<callback::Waiter<Status, bool>>(Status::OK);
  std::vector<ObjectIdentifier> added_values;
  for (auto const& journal_entry : journal_entries_) {
    if (journal_entry.second.deleted) {
      continue;
    }
    added_values.push_back(journal_entry.second.entry.object_identifier);
    page_storage_->ObjectIsUntracked(added_values.back(), waiter->NewCallback());
  }
  waiter->Finalize([added_values = std::move(added_values), callback = std::move(callback)](
                       Status status, std::vector<bool> is_untracked) {
    if (status != Status::OK) {
      callback(status, {});
      return;
    }
    FXL_DCHECK(added_values.size() == is_untracked.size());

    // Only untracked objects should be synced.
    std::vector<ObjectIdentifier> objects_to_sync;
    for (size_t i = 0; i < is_untracked.size(); ++i) {
      if (is_untracked[i]) {
        objects_to_sync.push_back(std::move(added_values[i]));
      }
    }
    callback(Status::OK, std::move(objects_to_sync));
  });
}

void JournalImpl::SetEntryIds(std::vector<EntryChange>* changes) {
  if (other_) {
    SetEntryIdsMergeCommit(changes);
  } else {
    SetEntryIdsSimpleCommit(changes);
  }
}

void JournalImpl::SetEntryIdsSimpleCommit(std::vector<EntryChange>* changes) {
  FXL_DCHECK(!other_);

  for (EntryChange& change : *changes) {
    if (!change.deleted) {
      change.entry.entry_id = page_storage_->GetEntryId();
    }
  }
}

void JournalImpl::SetEntryIdsMergeCommit(std::vector<EntryChange>* changes) {
  FXL_DCHECK(other_);

  // Serialize the list of changes.
  std::string operation_list;
  if (cleared_ == JournalContainsClearOperation::YES) {
    operation_list = "cleared";
  }
  for (const EntryChange& change : *changes) {
    const Entry& entry = change.entry;
    std::string entry_content;
    if (!change.deleted) {
      entry_content = SafeConcatenation({entry.priority == KeyPriority::EAGER ? "E" : "L",
                                         EncodeObjectIdentifier(entry.object_identifier)});
    }
    operation_list.append(
        SafeConcatenation({entry.key, change.deleted ? "D" : "U", entry_content}));
  }

  for (EntryChange& change : *changes) {
    if (!change.deleted) {
      change.entry.entry_id = page_storage_->GetEntryIdForMerge(change.entry.key, base_->GetId(),
                                                                other_->GetId(), operation_list);
    }
  }
}

}  // namespace storage
