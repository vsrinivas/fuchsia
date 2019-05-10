// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/p2p_sync/impl/commit_batch.h"

#include <lib/callback/scoped_callback.h>

namespace p2p_sync {

CommitBatch::CommitBatch(std::string device, Delegate* delegate,
                         storage::PageStorage* storage)
    : device_(std::move(device)),
      delegate_(delegate),
      storage_(storage),
      weak_factory_(this) {}

void CommitBatch::set_on_empty(fit::closure on_empty) {
  on_empty_ = std::move(on_empty);
}

void CommitBatch::AddToBatch(
    std::vector<storage::PageStorage::CommitIdAndBytes> new_commits) {
  // New commits are supposed to be the parents of the already inserted ones. We
  // insert them before to ensure parents are processed before children.
  //
  // This insertion may be suboptimal in some cases, for instance when a merge
  // commit's parents are related to each other. In that case, we may request
  // (and insert) multiple times the same commit. A better way would be to sort
  // these commits by generation before inserting, but we don't have access to
  // this information here.
  commits_.insert(commits_.begin(),
                  std::make_move_iterator(new_commits.begin()),
                  std::make_move_iterator(new_commits.end()));

  std::vector<storage::PageStorage::CommitIdAndBytes> out;
  out.reserve(commits_.size());
  for (const auto& commit : commits_) {
    out.emplace_back(commit.id, commit.bytes);
  }

  storage_->AddCommitsFromSync(
      std::move(out), storage::ChangeSource::P2P,
      callback::MakeScoped(
          weak_factory_.GetWeakPtr(),
          [this](ledger::Status status,
                 std::vector<storage::CommitId> missing_ids) {
            if (status == ledger::Status::OK) {
              if (on_empty_) {
                on_empty_();
              }
              return;
            }
            if (status == ledger::Status::INTERNAL_NOT_FOUND &&
                !missing_ids.empty()) {
              delegate_->RequestCommits(device_, std::move(missing_ids));
              return;
            }
            FXL_LOG(ERROR) << "Error while adding commits, aborting batch: "
                           << status;
            if (on_empty_) {
              on_empty_();
            }
          }));
}

}  // namespace p2p_sync
