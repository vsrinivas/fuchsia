// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/journal_impl.h"

#include <functional>
#include <map>
#include <string>
#include <utility>

#include <lib/callback/waiter.h>
#include <lib/fit/function.h>
#include <src/lib/fxl/memory/ref_ptr.h>

#include "src/ledger/bin/storage/impl/btree/builder.h"
#include "src/ledger/bin/storage/impl/btree/tree_node.h"
#include "src/ledger/bin/storage/impl/commit_impl.h"
#include "src/ledger/bin/storage/public/commit.h"

namespace storage {

namespace {
class JournalEntriesIterator : public Iterator<const storage::EntryChange> {
 public:
  JournalEntriesIterator(std::map<std::string, EntryChange>& journal_entries)
      : it_(journal_entries.begin()), end_(journal_entries.end()) {}

  ~JournalEntriesIterator() override {}

  Iterator<const storage::EntryChange>& Next() override {
    FXL_DCHECK(Valid()) << "Iterator::Next iterator not valid";
    ++it_;
    return *this;
  }

  bool Valid() const override { return it_ != end_; }

  Status GetStatus() const override { return Status::OK; }

  const storage::EntryChange& operator*() const override { return it_->second; }
  const storage::EntryChange* operator->() const override {
    return &(it_->second);
  }

 private:
  std::map<std::string, EntryChange>::const_iterator it_;
  std::map<std::string, EntryChange>::const_iterator end_;

  FXL_DISALLOW_COPY_AND_ASSIGN(JournalEntriesIterator);
};
}  // namespace

JournalImpl::JournalImpl(Token /* token */, ledger::Environment* environment,
                         PageStorageImpl* page_storage,
                         std::unique_ptr<const storage::Commit> base)
    : environment_(environment),
      page_storage_(page_storage),
      base_(std::move(base)),
      committed_(false) {}

JournalImpl::~JournalImpl() {}

std::unique_ptr<Journal> JournalImpl::Simple(
    ledger::Environment* environment, PageStorageImpl* page_storage,
    std::unique_ptr<const storage::Commit> base) {
  FXL_DCHECK(base);

  return std::make_unique<JournalImpl>(Token(), environment, page_storage,
                                       std::move(base));
}

std::unique_ptr<Journal> JournalImpl::Merge(
    ledger::Environment* environment, PageStorageImpl* page_storage,
    std::unique_ptr<const storage::Commit> base,
    std::unique_ptr<const storage::Commit> other) {
  FXL_DCHECK(base);
  FXL_DCHECK(other);
  auto journal = std::make_unique<JournalImpl>(Token(), environment,
                                               page_storage, std::move(base));
  journal->other_ = std::move(other);
  return journal;
}

void JournalImpl::Commit(
    fit::function<void(Status, std::unique_ptr<const storage::Commit>)>
        callback) {
  FXL_DCHECK(!committed_);
  committed_ = true;

  std::vector<std::unique_ptr<const storage::Commit>> parents;
  if (other_) {
    parents.reserve(2);
    parents.push_back(std::move(base_));
    parents.push_back(std::move(other_));
  } else {
    parents.reserve(1);
    parents.push_back(std::move(base_));
  }

  auto changes = std::make_unique<JournalEntriesIterator>(journal_entries_);

  if (cleared_ == JournalContainsClearOperation::NO) {
    // The journal doesn't contain the clear operation. The changes
    // recorded on the journal need to be executed over the content of
    // the first parent.
    ObjectIdentifier root_identifier = parents[0]->GetRootIdentifier();
    CreateCommitFromChanges(std::move(parents), std::move(root_identifier),
                            std::move(changes), std::move(callback));
    return;
  }

  // The journal contains the clear operation. The changes recorded on the
  // journal need to be executed over an empty page.
  btree::TreeNode::Empty(
      page_storage_,
      [this, parents = std::move(parents), changes = std::move(changes),
       callback = std::move(callback)](
          Status status, ObjectIdentifier root_identifier) mutable {
        if (status != Status::OK) {
          callback(status, nullptr);
          return;
        }
        CreateCommitFromChanges(std::move(parents), std::move(root_identifier),
                                std::move(changes), std::move(callback));
      });
}

void JournalImpl::Put(convert::ExtendedStringView key,
                      ObjectIdentifier object_identifier,
                      KeyPriority priority) {
  FXL_DCHECK(!committed_);
  EntryChange change;
  change.entry = {key.ToString(), std::move(object_identifier), priority};
  change.deleted = false;
  journal_entries_[key.ToString()] = std::move(change);
}

void JournalImpl::Delete(convert::ExtendedStringView key) {
  FXL_DCHECK(!committed_);
  EntryChange change;
  change.entry = {key.ToString(), ObjectIdentifier(), KeyPriority::EAGER};
  change.deleted = true;
  journal_entries_[key.ToString()] = std::move(change);
}

void JournalImpl::Clear() {
  FXL_DCHECK(!committed_);
  cleared_ = JournalContainsClearOperation::YES;
  journal_entries_.clear();
}

void JournalImpl::CreateCommitFromChanges(
    std::vector<std::unique_ptr<const storage::Commit>> parents,
    ObjectIdentifier root_identifier,
    std::unique_ptr<Iterator<const EntryChange>> changes,
    fit::function<void(Status, std::unique_ptr<const storage::Commit>)>
        callback) {
  btree::ApplyChanges(
      environment_->coroutine_service(), page_storage_,
      std::move(root_identifier), std::move(changes),
      [this, parents = std::move(parents), callback = std::move(callback)](
          Status status, ObjectIdentifier object_identifier,
          std::set<ObjectIdentifier> new_nodes) mutable {
        if (status != Status::OK) {
          callback(status, nullptr);
          return;
        }
        // If the commit is a no-op, return early, without creating a new
        // commit.
        if (parents.size() == 1 &&
            parents.front()->GetRootIdentifier() == object_identifier) {
          // |new_nodes| can be ignored here. If a clear operation has been
          // executed and the state has then been restored to the one before the
          // transaction, |ApplyChanges| might have re-created some nodes that
          // already exist. Because they already exist in a pre-existing commit,
          // there is no need to update their state.
          callback(Status::OK, std::move(parents.front()));
          return;
        }
        std::unique_ptr<const storage::Commit> commit =
            CommitImpl::FromContentAndParents(environment_->clock(),
                                              page_storage_, object_identifier,
                                              std::move(parents));
        GetObjectsToSync(
            [this, new_nodes = std::move(new_nodes), commit = std::move(commit),
             callback = std::move(callback)](
                Status status,
                std::vector<ObjectIdentifier> objects_to_sync) mutable {
              if (status != Status::OK) {
                callback(status, nullptr);
                return;
              }

              objects_to_sync.reserve(objects_to_sync.size() +
                                      new_nodes.size());
              // TODO(qsr): When using C++17, move data out of the set using
              // extract.
              objects_to_sync.insert(objects_to_sync.end(), new_nodes.begin(),
                                     new_nodes.end());
              page_storage_->AddCommitFromLocal(
                  commit->Clone(), std::move(objects_to_sync),
                  [commit = std::move(commit),
                   callback = std::move(callback)](Status status) mutable {
                    if (status != Status::OK) {
                      callback(status, nullptr);
                      return;
                    }
                    callback(Status::OK, std::move(commit));
                  });
            });
      });
}

void JournalImpl::GetObjectsToSync(
    fit::function<void(Status status,
                       std::vector<ObjectIdentifier> objects_to_sync)>
        callback) {
  auto waiter = fxl::MakeRefCounted<callback::Waiter<Status, bool>>(Status::OK);
  std::vector<ObjectIdentifier> added_values;
  for (auto const& journal_entry : journal_entries_) {
    if (journal_entry.second.deleted) {
      continue;
    }
    added_values.push_back(journal_entry.second.entry.object_identifier);
    page_storage_->ObjectIsUntracked(added_values.back(),
                                     waiter->NewCallback());
  }
  waiter->Finalize(
      [added_values = std::move(added_values), callback = std::move(callback)](
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

}  // namespace storage
