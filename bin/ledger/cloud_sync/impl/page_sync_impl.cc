// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_sync/impl/page_sync_impl.h"

#include <algorithm>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "apps/ledger/src/storage/public/types.h"
#include "lib/ftl/logging.h"

namespace cloud_sync {

PageSyncImpl::PageSyncImpl(ftl::RefPtr<ftl::TaskRunner> task_runner,
                           storage::PageStorage* storage,
                           cloud_provider::CloudProvider* cloud_provider,
                           std::unique_ptr<backoff::Backoff> backoff,
                           ftl::Closure error_callback)
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
  // Remove the watchers and the delegate, if they were not already removed on
  // hard error.
  if (!errored_) {
    storage_->SetSyncDelegate(nullptr);
    storage_->RemoveCommitWatcher(this);
    cloud_provider_->UnwatchCommits(this);
  }
}

void PageSyncImpl::Start() {
  FTL_DCHECK(!started_);
  started_ = true;
  storage_->SetSyncDelegate(this);

  TryStartDownload();

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
  local_watch_set_ = true;
}

void PageSyncImpl::SetOnIdle(ftl::Closure on_idle_callback) {
  FTL_DCHECK(!on_idle_callback_);
  on_idle_callback_ = std::move(on_idle_callback);
}

bool PageSyncImpl::IsIdle() {
  return commit_uploads_.empty() && download_list_retrieved &&
         commit_downloads_.empty();
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

void PageSyncImpl::GetObject(
    storage::ObjectIdView object_id,
    std::function<void(storage::Status status,
                       uint64_t size,
                       mx::datapipe_consumer data)> callback) {
  cloud_provider_->GetObject(
      object_id, [callback](cloud_provider::Status status, uint64_t size,
                            mx::datapipe_consumer data) {
        if (status != cloud_provider::Status::OK) {
          // TODO(ppi), LE-82: distinguish network errors or not found once the
          // cloud provider can do this.
          FTL_LOG(WARNING) << "Fetching remote object failed with status: "
                           << static_cast<int>(status);
          callback(storage::Status::IO_ERROR, 0, mx::datapipe_consumer());
          return;
        }

        callback(storage::Status::OK, size, std::move(data));
      });
}

void PageSyncImpl::OnRemoteCommit(cloud_provider::Commit commit,
                                  std::string timestamp) {
  EnqueueDownload(
      cloud_provider::Record(std::move(commit), std::move(timestamp)));
}

void PageSyncImpl::OnError() {}

void PageSyncImpl::TryStartDownload() {
  // Retrieve the server-side timestamp of the last commit we received.
  std::string last_commit_ts;
  auto status = storage_->GetSyncMetadata(&last_commit_ts);
  // NOT_FOUND means that we haven't persisted the state yet, e.g. because we
  // haven't received any remote commits yet. In this case an empty timestamp is
  // the right value.
  if (status != storage::Status::OK && status != storage::Status::NOT_FOUND) {
    HandleError("Failed to retrieve the sync metadata.");
    return;
  }

  // TODO(ppi): handle pagination when the response is huge.
  cloud_provider_->GetCommits(
      last_commit_ts,
      [this, last_commit_ts](cloud_provider::Status cloud_status,
                             std::vector<cloud_provider::Record> records) {
        if (cloud_status != cloud_provider::Status::OK) {
          // Fetching the remote commits failed, schedule a retry.
          task_runner_->PostDelayedTask(
              [weak_this = weak_factory_.GetWeakPtr()]() {
                if (weak_this && !weak_this->errored_) {
                  weak_this->TryStartDownload();
                }
              },
              backoff_->GetNext());
          return;
        }
        backoff_->Reset();
        for (auto& record : records) {
          EnqueueDownload(std::move(record));
        }
        download_list_retrieved = true;
        CheckIdle();

        // Register a cloud watcher for the new commits. This currently mixes
        // connection errors with data errors in one OnError() callback, see
        // LE-76. TODO(ppi): Retry setting the watcher on connection errors.
        cloud_provider_->WatchCommits(last_commit_ts, this);
        remote_watch_set_ = true;
      });
}

void PageSyncImpl::EnqueueDownload(cloud_provider::Record record) {
  // If there are no commits currently being downloaded, start the download
  // after enqueing this one.
  const bool start_after_adding = commit_downloads_.empty();

  commit_downloads_.emplace(
      storage_, std::move(record),
      [this] {
        commit_downloads_.pop();
        if (!commit_downloads_.empty()) {
          commit_downloads_.front().Start();
        } else {
          CheckIdle();
        }
      },
      [this] { HandleError("Failed to persist a remote commit in storage"); });

  if (start_after_adding) {
    commit_downloads_.front().Start();
  }
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
                            } else {
                              CheckIdle();
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
  if (local_watch_set_) {
    storage_->RemoveCommitWatcher(this);
  }
  if (remote_watch_set_) {
    cloud_provider_->UnwatchCommits(this);
  }
  storage_->SetSyncDelegate(nullptr);
  error_callback_();
  errored_ = true;
}

void PageSyncImpl::CheckIdle() {
  if (on_idle_callback_ && IsIdle()) {
    on_idle_callback_();
  }
}

}  // namespace cloud_sync
