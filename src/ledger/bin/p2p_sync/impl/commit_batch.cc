// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/p2p_sync/impl/commit_batch.h"

#include <utility>
#include <vector>

#include "src/ledger/lib/callback/waiter.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/ledger/lib/memory/ref_ptr.h"
#include "src/lib/callback/scoped_callback.h"

namespace p2p_sync {

CommitBatch::CommitBatch(p2p_provider::P2PClientId device, Delegate* delegate,
                         storage::PageStorage* storage)
    : device_(std::move(device)), delegate_(delegate), storage_(storage), weak_factory_(this) {}

void CommitBatch::SetOnDiscardable(fit::closure on_discardable) {
  on_discardable_ = std::move(on_discardable);
}

bool CommitBatch::IsDiscardable() const { return discardable_; }

void CommitBatch::AddToBatch(std::vector<storage::PageStorage::CommitIdAndBytes> new_commits) {
  // Ask the storage for the generation and missing parents of the new commits.
  auto waiter =
      ledger::MakeRefCounted<ledger::Waiter<ledger::Status, storage::PageStorage::CommitIdAndBytes,
                                            uint64_t, std::vector<storage::CommitId>>>(
          ledger::Status::OK);

  for (auto& commit : new_commits) {
    if (commits_.find(commit.id) != commits_.end()) {
      continue;
    }
    auto commit_owner = std::make_unique<storage::PageStorage::CommitIdAndBytes>(std::move(commit));
    auto& commit_ref = *commit_owner.get();
    storage_->GetGenerationAndMissingParents(
        commit_ref, [commit_owner = std::move(commit_owner), callback = waiter->NewCallback()](
                        ledger::Status status, uint64_t generation,
                        std::vector<storage::CommitId> parents) mutable {
          callback(status, std::move(*commit_owner), generation, std::move(parents));
        });
  }

  waiter->Finalize(callback::MakeScoped(
      weak_factory_.GetWeakPtr(),
      [this](ledger::Status status,
             std::vector<std::tuple<storage::PageStorage::CommitIdAndBytes, uint64_t,
                                    std::vector<storage::CommitId>>>
                 commits_to_add) {
        if (status != ledger::Status::OK) {
          LEDGER_LOG(ERROR)
              << "Error while getting commit parents and generations, aborting batch: " << status;
          if (on_discardable_) {
            on_discardable_();
          }
          return;
        }
        // Collect missing parents and add the commits to the batch.
        std::set<storage::CommitId> all_missing_parents;
        for (auto& [commit, generation, missing_parents] : commits_to_add) {
          std::move(missing_parents.begin(), missing_parents.end(),
                    std::inserter(all_missing_parents, all_missing_parents.begin()));
          requested_commits_.erase(commit.id);
          commits_[std::move(commit.id)] = std::make_pair(std::move(commit.bytes), generation);
        }
        // Some missing parents may already be requested or present in the batch.
        std::vector<storage::CommitId> new_commits_to_request;
        for (const storage::CommitId& missing : all_missing_parents) {
          if (commits_.find(missing) != commits_.end()) {
            continue;
          }
          if (requested_commits_.insert(missing).second) {
            new_commits_to_request.push_back(missing);
          }
        }
        if (!new_commits_to_request.empty()) {
          delegate_->RequestCommits(device_, std::move(new_commits_to_request));
        }
        // Attempt to add the batch.
        AddCommits();
      }));
}

void CommitBatch::MarkPeerReady() {
  if (!peer_is_ready_) {
    peer_is_ready_ = true;
    AddCommits();
  }
}

void CommitBatch::AddCommits() {
  if (!peer_is_ready_ || commits_.empty() || !requested_commits_.empty()) {
    return;
  }

  // All parents are present, either in storage or in the batch. Sort the commits by generation: if
  // the commit tree is valid, this will put parents before children; otherwise the batch will be
  // rejected by the storage.
  std::vector<std::pair<uint64_t, storage::PageStorage::CommitIdAndBytes>> commits_and_generation;
  commits_and_generation.reserve(commits_.size());
  std::transform(commits_.begin(), commits_.end(), std::back_inserter(commits_and_generation),
                 [](auto& entry) {
                   auto& [id, data] = entry;
                   auto& [bytes, generation] = data;
                   return std::make_pair(generation, storage::PageStorage::CommitIdAndBytes(
                                                         std::move(id), std::move(bytes)));
                 });
  commits_.clear();
  std::sort(commits_and_generation.begin(), commits_and_generation.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

  std::vector<storage::PageStorage::CommitIdAndBytes> commits;
  commits.reserve(commits_and_generation.size());
  std::transform(
      commits_and_generation.begin(), commits_and_generation.end(), std::back_inserter(commits),
      [](auto& commit_and_generation) { return std::move(commit_and_generation.second); });

  storage_->AddCommitsFromSync(
      std::move(commits), storage::ChangeSource::P2P,
      callback::MakeScoped(weak_factory_.GetWeakPtr(), [this](ledger::Status status) {
        if (status != ledger::Status::OK) {
          LEDGER_LOG(ERROR) << "Error while adding commits, aborting batch: " << status;
        }
        discardable_ = true;
        if (on_discardable_) {
          on_discardable_();
        }
      }));
}

}  // namespace p2p_sync
