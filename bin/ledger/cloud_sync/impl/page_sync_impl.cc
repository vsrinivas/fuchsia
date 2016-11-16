// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_sync/impl/page_sync_impl.h"

#include <algorithm>
#include <map>
#include <memory>
#include <vector>

#include "apps/ledger/src/storage/public/types.h"
#include "lib/ftl/logging.h"

namespace cloud_sync {

PageSyncImpl::PageSyncImpl(ftl::RefPtr<ftl::TaskRunner> task_runner,
                           storage::PageStorage* storage,
                           cloud_provider::CloudProvider* cloud_provider,
                           std::unique_ptr<backoff::Backoff> backoff,
                           std::function<void()> error_callback)
    : task_runner_(task_runner),
      storage_(storage),
      cloud_provider_(cloud_provider),
      backoff_(std::move(backoff)),
      error_callback_(error_callback),
      weak_factory_(this) {
  FTL_DCHECK(storage);
  FTL_DCHECK(cloud_provider);
}

PageSyncImpl::~PageSyncImpl() {
  // Unregister the storage watcher, if wasn't done already.
  if (!errored_) {
    storage_->RemoveCommitWatcher(this);
  }
}

void PageSyncImpl::Start() {
  FTL_DCHECK(!started_);
  started_ = true;

  // Retrieve the backlog of the existing unsynced commits and enqueue them for
  // upload.
  // TODO(ppi): either switch to a paginating API or (better?) ensure that long
  // backlogs of local commits are squashed in storage, as otherwise the list of
  // commits can be possibly very big.
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  if (storage_->GetUnsyncedCommits(&commits) != storage::Status::OK) {
    HandleError("Failed to retrieve the unsynced commits");
    return;
  }

  for (auto& commit : commits) {
    EnqueueUpload(std::move(commit));
  }

  // Subscribe to notifications about new commits in Storage.
  storage_->AddCommitWatcher(this);
}

void PageSyncImpl::OnNewCommit(const storage::Commit& commit,
                               storage::ChangeSource source) {
  // Only upload the locally created commits.
  // TODO(ppi): revisit this when we have p2p sync, too.
  if (source != storage::ChangeSource::LOCAL) {
    return;
  }

  EnqueueUpload(commit.Clone());
}

void PageSyncImpl::EnqueueUpload(
    std::unique_ptr<const storage::Commit> commit) {
  // If there are no commits currently being uploaded, start the upload after
  // enqueing this one.
  const bool start_after_adding = commit_uploads_.empty();

  commit_uploads_.emplace(storage_, cloud_provider_, std::move(commit),
                          [this] {
                            // Upload succeeded, reset the backoff delay.
                            backoff_->Reset();

                            commit_uploads_.pop();
                            if (!commit_uploads_.empty()) {
                              commit_uploads_.front().Start();
                            }
                          },
                          [this] {
                            task_runner_->PostDelayedTask(
                                [weak_this = weak_factory_.GetWeakPtr()]() {
                                  if (weak_this && !weak_this->errored_) {
                                    weak_this->commit_uploads_.front().Start();
                                  }
                                },
                                backoff_->GetNext());
                          });

  if (start_after_adding) {
    commit_uploads_.front().Start();
  }
}

void PageSyncImpl::HandleError(const char error_description[]) {
  FTL_LOG(ERROR) << error_description << " Stopping sync.";
  error_callback_();
  storage_->RemoveCommitWatcher(this);
  errored_ = true;
}

}  // namespace cloud_sync
