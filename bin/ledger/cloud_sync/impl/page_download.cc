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

PageDownload::PageDownload(
    callback::ScopedTaskRunner* task_runner,
    storage::PageStorage* storage,
    encryption::EncryptionService* encryption_service,
    cloud_provider_firebase::PageCloudHandler* cloud_provider,
    Delegate* delegate)
    : task_runner_(task_runner),
      storage_(storage),
      encryption_service_(encryption_service),
      cloud_provider_(cloud_provider),
      delegate_(delegate),
      log_prefix_(fxl::Concatenate(
          {"Page ", convert::ToHex(storage->GetId()), " download sync: "})) {}

PageDownload::~PageDownload() {
  storage_->SetSyncDelegate(nullptr);
  if (commit_state_ != DOWNLOAD_PERMANENT_ERROR) {
    cloud_provider_->UnwatchCommits(this);
  }
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

        delegate_->GetAuthToken(
            [ this, last_commit_ts =
                        std::move(last_commit_ts) ](std::string auth_token) {
              // TODO(ppi): handle pagination when the response is huge.
              cloud_provider_->GetCommits(
                  auth_token, last_commit_ts,
                  [this](cloud_provider_firebase::Status cloud_status,
                         std::vector<cloud_provider_firebase::Record> records) {
                    if (cloud_status != cloud_provider_firebase::Status::OK) {
                      // Fetching the remote commits failed, schedule a retry.
                      FXL_LOG(WARNING)
                          << log_prefix_
                          << "fetching the remote commits failed due to a "
                          << "connection error, status: " << cloud_status
                          << ", retrying.";
                      SetCommitState(DOWNLOAD_TEMPORARY_ERROR);
                      delegate_->Retry([this] { StartDownload(); });
                      return;
                    }
                    delegate_->Success();

                    if (records.empty()) {
                      // If there is no remote commits to add, announce that
                      // we're done.
                      FXL_VLOG(1)
                          << log_prefix_
                          << "initial sync finished, no new remote commits";
                      BacklogDownloaded();
                    } else {
                      FXL_VLOG(1)
                          << log_prefix_ << "retrieved " << records.size()
                          << " (possibly) new remote commits, "
                          << "adding them to storage.";
                      // If not, fire the backlog download callback when the
                      // remote commits are downloaded.
                      const auto record_count = records.size();
                      DownloadBatch(std::move(records), [this, record_count] {
                        FXL_VLOG(1)
                            << log_prefix_ << "initial sync finished, added "
                            << record_count << " remote commits.";
                        BacklogDownloaded();
                      });
                    }
                  });
            },
            [this] {
              HandleError(
                  "Failed to retrieve the auth token to download commit "
                  "backlog.");
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

        delegate_->GetAuthToken(
            [ this, is_retry, last_commit_ts = std::move(last_commit_ts) ](
                std::string auth_token) {
              cloud_provider_->WatchCommits(auth_token, last_commit_ts, this);
              SetCommitState(DOWNLOAD_IDLE);
              if (is_retry) {
                FXL_LOG(INFO) << log_prefix_ << "Cloud watcher re-established";
              }
            },
            [this] {
              HandleError(
                  "Failed to retrieve the auth token to set a cloud watcher.");
            });
      }));
}

void PageDownload::OnRemoteCommits(
    std::vector<cloud_provider_firebase::Record> records) {
  if (batch_download_) {
    // If there is already a commit batch being downloaded, save the new commits
    // to be downloaded when it is done.
    std::move(records.begin(), records.end(),
              std::back_inserter(commits_to_download_));
    return;
  }
  SetCommitState(DOWNLOAD_IN_PROGRESS);
  DownloadBatch(std::move(records), nullptr);
}

void PageDownload::OnConnectionError() {
  FXL_DCHECK(commit_state_ == DOWNLOAD_IDLE ||
             commit_state_ == DOWNLOAD_IN_PROGRESS);
  // Reset the watcher and schedule a retry.
  cloud_provider_->UnwatchCommits(this);
  SetCommitState(DOWNLOAD_TEMPORARY_ERROR);
  FXL_LOG(WARNING)
      << log_prefix_
      << "Connection error in the remote commit watcher, retrying.";
  delegate_->Retry([this] { SetRemoteWatcher(true); });
}

void PageDownload::OnTokenExpired() {
  FXL_DCHECK(commit_state_ == DOWNLOAD_IDLE ||
             commit_state_ == DOWNLOAD_IN_PROGRESS);
  // Reset the watcher and schedule a retry.
  cloud_provider_->UnwatchCommits(this);
  SetCommitState(DOWNLOAD_TEMPORARY_ERROR);
  FXL_LOG(INFO) << log_prefix_ << "Firebase token expired, refreshing.";
  delegate_->Retry([this] { SetRemoteWatcher(true); });
}

void PageDownload::OnMalformedNotification() {
  HandleError("Received a malformed remote commit notification.");
}

void PageDownload::DownloadBatch(
    std::vector<cloud_provider_firebase::Record> records,
    fxl::Closure on_done) {
  FXL_DCHECK(!batch_download_);
  batch_download_ = std::make_unique<BatchDownload>(
      storage_, encryption_service_, std::move(records),
      [this, on_done = std::move(on_done)] {
        if (on_done) {
          on_done();
        }
        batch_download_.reset();

        if (commits_to_download_.empty()) {
          if (!on_done) {
            SetCommitState(DOWNLOAD_IDLE);
          }
          return;
        }
        auto commits = std::move(commits_to_download_);
        commits_to_download_.clear();
        DownloadBatch(std::move(commits), nullptr);
      },
      [this] { HandleError("Failed to persist a remote commit in storage"); });
  batch_download_->Start();
}

void PageDownload::GetObject(
    storage::ObjectDigestView object_digest,
    std::function<void(storage::Status status, uint64_t size, zx::socket data)>
        callback) {
  current_get_object_calls_++;
  delegate_->GetAuthToken(
      [ this, object_digest = object_digest.ToString(),
        callback ](std::string auth_token) mutable {
        cloud_provider_->GetObject(
            auth_token, object_digest,
            [ this, object_digest, callback = std::move(callback) ](
                cloud_provider_firebase::Status status, uint64_t size,
                zx::socket data) mutable {
              if (status == cloud_provider_firebase::Status::NETWORK_ERROR) {
                FXL_LOG(WARNING) << log_prefix_
                                 << "GetObject() failed due to a connection "
                                    "error, retrying.";
                current_get_object_calls_--;
                delegate_->Retry([
                  this, object_digest = std::move(object_digest),
                  callback = std::move(callback)
                ] { GetObject(object_digest, callback); });
                return;
              }

              delegate_->Success();
              if (status != cloud_provider_firebase::Status::OK) {
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
      },
      [this, callback] {
        FXL_LOG(ERROR) << log_prefix_ << "Failed to retrieve the auth token, "
                       << "cannot download the object.";
        callback(storage::Status::IO_ERROR, 0, zx::socket());
        // Auth errors in object retrieval are ignored. For some reason.
        // See LE-315.
        current_get_object_calls_--;
      });
}

void PageDownload::HandleError(const char error_description[]) {
  FXL_LOG(ERROR) << log_prefix_ << error_description << " Stopping sync.";
  if (commit_state_ == DOWNLOAD_IDLE || commit_state_ == DOWNLOAD_IN_PROGRESS) {
    cloud_provider_->UnwatchCommits(this);
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
