// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cloud_sync/impl/page_download.h"

#include "lib/fxl/strings/concatenate.h"
#include "peridot/bin/ledger/cloud_sync/impl/constants.h"

namespace cloud_sync {
namespace {
DownloadSyncState GetMergedState(DownloadSyncState commit_state,
                                 int current_get_object_calls) {
  if (commit_state != DOWNLOAD_IDLE) {
    return commit_state;
  }
  return current_get_object_calls == 0 ? DOWNLOAD_IDLE : DOWNLOAD_IN_PROGRESS;
}
}  // namespace

PageDownload::PageDownload(callback::ScopedTaskRunner* task_runner,
                           storage::PageStorage* storage,
                           encryption::EncryptionService* encryption_service,
                           cloud_provider::PageCloudPtr* page_cloud,
                           Delegate* delegate,
                           std::unique_ptr<backoff::Backoff> backoff)
    : task_runner_(task_runner),
      storage_(storage),
      encryption_service_(encryption_service),
      page_cloud_(page_cloud),
      delegate_(delegate),
      backoff_(std::move(backoff)),
      log_prefix_(fxl::Concatenate(
          {"Page ", convert::ToHex(storage->GetId()), " download sync: "})),
      watcher_binding_(this) {}

PageDownload::~PageDownload() {
  storage_->SetSyncDelegate(nullptr);
}

void PageDownload::StartDownload() {
  SetCommitState(DOWNLOAD_BACKLOG);

  storage_->SetSyncDelegate(this);

  // Retrieve the server-side timestamp of the last commit we received.
  storage_->GetSyncMetadata(
      kTimestampKey,
      task_runner_->MakeScoped([this](storage::Status status,
                                      std::string last_commit_ts) {
        // NOT_FOUND means that we haven't persisted the state yet, e.g. because
        // we haven't received any remote commits yet. In this case an empty
        // timestamp is the right value.
        if (status != storage::Status::OK &&
            status != storage::Status::NOT_FOUND) {
          HandleError("Failed to retrieve the sync metadata.");
          return;
        }
        if (last_commit_ts.empty()) {
          FXL_VLOG(1) << log_prefix_ << "starting sync for the first time, "
                      << "retrieving all remote commits";
        } else {
          // TODO(ppi): print the timestamp out as human-readable wall time.
          FXL_VLOG(1) << log_prefix_ << "starting sync again, "
                      << "retrieving commits uploaded after: "
                      << last_commit_ts;
        }

        fidl::Array<uint8_t> position_token;
        if (!last_commit_ts.empty()) {
          position_token = convert::ToArray(last_commit_ts);
        }
        // TODO(ppi): handle pagination when the response is huge.
        (*page_cloud_)
            ->GetCommits(
                std::move(position_token),
                [this](cloud_provider::Status cloud_status,
                       fidl::Array<cloud_provider::CommitPtr> commits,
                       fidl::Array<uint8_t> position_token) {
                  if (cloud_status != cloud_provider::Status::OK) {
                    // Fetching the remote commits failed, schedule a retry.
                    FXL_LOG(WARNING)
                        << log_prefix_
                        << "fetching the remote commits failed due to a "
                        << "connection error, status: " << cloud_status
                        << ", retrying.";
                    SetCommitState(DOWNLOAD_TEMPORARY_ERROR);
                    RetryWithBackoff([this] { StartDownload(); });
                    return;
                  }
                  backoff_->Reset();

                  if (commits.empty()) {
                    // If there is no remote commits to add, announce that
                    // we're done.
                    FXL_VLOG(1)
                        << log_prefix_
                        << "initial sync finished, no new remote commits";
                    BacklogDownloaded();
                  } else {
                    FXL_VLOG(1) << log_prefix_ << "retrieved " << commits.size()
                                << " (possibly) new remote commits, "
                                << "adding them to storage.";
                    // If not, fire the backlog download callback when the
                    // remote commits are downloaded.
                    const auto commit_count = commits.size();
                    DownloadBatch(std::move(commits), std::move(position_token),
                                  [this, commit_count] {
                                    FXL_VLOG(1)
                                        << log_prefix_
                                        << "initial sync finished, added "
                                        << commit_count << " remote commits.";
                                    BacklogDownloaded();
                                  });
                  }
                });
      }));
}

void PageDownload::BacklogDownloaded() {
  SetRemoteWatcher(false);
}

void PageDownload::SetRemoteWatcher(bool is_retry) {
  FXL_DCHECK(commit_state_ == DOWNLOAD_BACKLOG ||
             commit_state_ == DOWNLOAD_TEMPORARY_ERROR)
      << "Current state: " << commit_state_;
  SetCommitState(DOWNLOAD_SETTING_REMOTE_WATCHER);
  // Retrieve the server-side timestamp of the last commit we received.
  std::string last_commit_ts;
  storage_->GetSyncMetadata(
      kTimestampKey,
      task_runner_->MakeScoped([this, is_retry](storage::Status status,
                                                std::string last_commit_ts) {
        if (status != storage::Status::OK &&
            status != storage::Status::NOT_FOUND) {
          HandleError("Failed to retrieve the sync metadata.");
          return;
        }

        fidl::Array<uint8_t> position_token;
        if (!last_commit_ts.empty()) {
          position_token = convert::ToArray(last_commit_ts);
        }
        cloud_provider::PageCloudWatcherPtr watcher;
        watcher_binding_.Bind(watcher.NewRequest());
        (*page_cloud_)
            ->SetWatcher(
                std::move(position_token), std::move(watcher),
                [this](auto status) {
                  // This should always succeed - any errors are reported
                  // through OnError().
                  if (status != cloud_provider::Status::OK) {
                    HandleError(
                        "Unexpected error when setting the PageCloudWatcher.");
                  }
                });
        SetCommitState(DOWNLOAD_IDLE);
        if (is_retry) {
          FXL_LOG(INFO) << log_prefix_ << "Cloud watcher re-established";
        }
      }));
}

void PageDownload::OnNewCommits(fidl::Array<cloud_provider::CommitPtr> commits,
                                fidl::Array<uint8_t> position_token,
                                const OnNewCommitsCallback& callback) {
  if (batch_download_) {
    // If there is already a commit batch being downloaded, save the new commits
    // to be downloaded when it is done.
    for (auto& commit : commits) {
      commits_to_download_.push_back(std::move(commit));
    }
    position_token_ = std::move(position_token);
    callback();
    return;
  }
  SetCommitState(DOWNLOAD_IN_PROGRESS);
  DownloadBatch(std::move(commits), std::move(position_token), callback);
}

void PageDownload::OnNewObject(fidl::Array<uint8_t> /*id*/,
                               zx::vmo /*data*/,
                               const OnNewObjectCallback& /*callback*/) {
  // No known cloud provider implementations use this method.
  // TODO(ppi): implement this method when we have such cloud provider
  // implementations.
  FXL_NOTIMPLEMENTED();
}

void PageDownload::OnError(cloud_provider::Status status) {
  FXL_DCHECK(commit_state_ == DOWNLOAD_IDLE ||
             commit_state_ == DOWNLOAD_IN_PROGRESS);
  if (status == cloud_provider::Status::NETWORK_ERROR ||
      status == cloud_provider::Status::AUTH_ERROR) {
    // Reset the watcher and schedule a retry.
    if (watcher_binding_.is_bound()) {
      watcher_binding_.Close();
    }
    SetCommitState(DOWNLOAD_TEMPORARY_ERROR);
    FXL_LOG(WARNING)
        << log_prefix_
        << "Connection error in the remote commit watcher, retrying.";
    RetryWithBackoff([this] { SetRemoteWatcher(true); });
    return;
  }

  if (status == cloud_provider::Status::PARSE_ERROR) {
    HandleError("Received a malformed remote commit notification.");
    return;
  }

  FXL_LOG(WARNING) << "Received unexpected error from PageCloudWatcher: "
                   << status;
  HandleError("Received unexpected error from PageCloudWatcher.");
}

void PageDownload::DownloadBatch(fidl::Array<cloud_provider::CommitPtr> commits,
                                 fidl::Array<uint8_t> position_token,
                                 fxl::Closure on_done) {
  FXL_DCHECK(!batch_download_);
  batch_download_ = std::make_unique<BatchDownload>(
      storage_, encryption_service_, std::move(commits),
      std::move(position_token),
      [this, on_done = std::move(on_done)] {
        if (on_done) {
          on_done();
        }
        batch_download_.reset();

        if (commits_to_download_.empty()) {
          // Don't set to idle if we're in process of setting the remote
          // watcher.
          if (commit_state_ == DOWNLOAD_IN_PROGRESS) {
            SetCommitState(DOWNLOAD_IDLE);
          }
          return;
        }
        auto commits = std::move(commits_to_download_);
        commits_to_download_ = fidl::Array<cloud_provider::CommitPtr>::New(0);
        DownloadBatch(std::move(commits), std::move(position_token_), nullptr);
      },
      [this] { HandleError("Failed to persist a remote commit in storage"); });
  batch_download_->Start();
}

void PageDownload::GetObject(
    storage::ObjectDigestView object_digest,
    std::function<void(storage::Status status, uint64_t size, zx::socket data)>
        callback) {
  current_get_object_calls_++;
  auto object_digest_str = object_digest.ToString();
  (*page_cloud_)
      ->GetObject(
          convert::ToArray(object_digest_str),
          [this, object_digest_str, callback = std::move(callback)](
              cloud_provider::Status status, uint64_t size,
              zx::socket data) mutable {
            if (status == cloud_provider::Status::NETWORK_ERROR ||
                status == cloud_provider::Status::AUTH_ERROR) {
              FXL_LOG(WARNING) << log_prefix_
                               << "GetObject() failed due to a connection "
                                  "error or stale auth token, retrying.";
              current_get_object_calls_--;
              RetryWithBackoff([
                this, object_digest = std::move(object_digest_str),
                callback = std::move(callback)
              ] { GetObject(object_digest, callback); });
              return;
            }

            backoff_->Reset();
            if (status != cloud_provider::Status::OK) {
              FXL_LOG(WARNING)
                  << log_prefix_
                  << "Fetching remote object failed with status: " << status;
              callback(storage::Status::IO_ERROR, 0, zx::socket());
              current_get_object_calls_--;
              return;
            }

            callback(storage::Status::OK, size, std::move(data));
            current_get_object_calls_--;
          });
}

void PageDownload::HandleError(const char error_description[]) {
  FXL_LOG(ERROR) << log_prefix_ << error_description << " Stopping sync.";
  if (watcher_binding_.is_bound()) {
    watcher_binding_.Close();
  }
  storage_->SetSyncDelegate(nullptr);
  SetCommitState(DOWNLOAD_PERMANENT_ERROR);
}

void PageDownload::SetCommitState(DownloadSyncState new_state) {
  if (new_state == commit_state_) {
    return;
  }
  // Notify only if the externally visible state changed.
  bool notify = GetMergedState(commit_state_, current_get_object_calls_) !=
                GetMergedState(new_state, current_get_object_calls_);
  commit_state_ = new_state;
  if (notify) {
    delegate_->SetDownloadState(
        GetMergedState(commit_state_, current_get_object_calls_));
  }
}

void PageDownload::RetryWithBackoff(fxl::Closure callable) {
  task_runner_->PostDelayedTask(
      [this, callable = std::move(callable)]() {
        if (this->commit_state_ != DOWNLOAD_PERMANENT_ERROR) {
          callable();
        }
      },
      backoff_->GetNext());
}

bool PageDownload::IsIdle() {
  switch (GetMergedState(commit_state_, current_get_object_calls_)) {
    case DOWNLOAD_STOPPED:
    case DOWNLOAD_IDLE:
    case DOWNLOAD_PERMANENT_ERROR:
      return true;
      break;
    case DOWNLOAD_BACKLOG:
    case DOWNLOAD_TEMPORARY_ERROR:
    case DOWNLOAD_SETTING_REMOTE_WATCHER:
    case DOWNLOAD_IN_PROGRESS:
      return false;
      break;
  }
}

}  // namespace cloud_sync
