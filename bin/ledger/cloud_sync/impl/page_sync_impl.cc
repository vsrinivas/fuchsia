// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_sync/impl/page_sync_impl.h"

#include <algorithm>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "apps/ledger/src/cloud_sync/impl/constants.h"
#include "apps/ledger/src/storage/public/types.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"

namespace cloud_sync {

PageSyncImpl::PageSyncImpl(
    fxl::RefPtr<fxl::TaskRunner> task_runner,
    storage::PageStorage* storage,
    cloud_provider_firebase::CloudProvider* cloud_provider,
    auth_provider::AuthProvider* auth_provider,
    std::unique_ptr<backoff::Backoff> backoff,
    fxl::Closure on_error,
    std::unique_ptr<SyncStateWatcher> ledger_watcher)
    : task_runner_(std::move(task_runner)),
      storage_(storage),
      cloud_provider_(cloud_provider),
      auth_provider_(auth_provider),
      backoff_(std::move(backoff)),
      on_error_(std::move(on_error)),
      log_prefix_("Page " + convert::ToHex(storage->GetId()) + " sync: "),
      ledger_watcher_(std::move(ledger_watcher)),
      weak_factory_(this) {
  FXL_DCHECK(storage_);
  FXL_DCHECK(cloud_provider_);
  FXL_DCHECK(auth_provider_);
}

PageSyncImpl::~PageSyncImpl() {
  // Remove the watchers and the delegate, if they were not already removed on
  // hard error.
  if (!errored_) {
    storage_->SetSyncDelegate(nullptr);
    storage_->RemoveCommitWatcher(this);
    cloud_provider_->UnwatchCommits(this);
  }

  if (on_delete_) {
    on_delete_();
  }
}

void PageSyncImpl::EnableUpload() {
  if (upload_enabled_) {
    return;
  }

  upload_enabled_ = true;
  StartUpload();
}

void PageSyncImpl::Start() {
  FXL_DCHECK(!started_);
  started_ = true;
  storage_->SetSyncDelegate(this);

  StartDownload();
}

void PageSyncImpl::SetOnIdle(fxl::Closure on_idle) {
  FXL_DCHECK(!on_idle_);
  FXL_DCHECK(!started_);
  on_idle_ = std::move(on_idle);
}

bool PageSyncImpl::IsIdle() {
  // TODO(nellyv): We should try to simplify the logic behind upload/download
  // states and IsIdle(). See LE-262.
  return !commits_to_upload_ &&
         (upload_state_ == UPLOAD_IDLE ||
          upload_state_ == WAIT_TOO_MANY_LOCAL_HEADS) &&
         download_list_retrieved_ && !batch_download_ &&
         upload_state_ != UPLOAD_PENDING && commits_to_download_.empty();
}

void PageSyncImpl::SetOnBacklogDownloaded(fxl::Closure on_backlog_downloaded) {
  FXL_DCHECK(!on_backlog_downloaded_);
  FXL_DCHECK(!started_);
  on_backlog_downloaded_ = on_backlog_downloaded;
}

void PageSyncImpl::SetSyncWatcher(SyncStateWatcher* watcher) {
  page_watcher_ = watcher;
  if (page_watcher_) {
    page_watcher_->Notify(download_state_, upload_state_);
  }
}

void PageSyncImpl::OnNewCommits(
    const std::vector<std::unique_ptr<const storage::Commit>>& /*commits*/,
    storage::ChangeSource source) {
  // Only upload the locally created commits.
  // TODO(ppi): revisit this when we have p2p sync, too.
  if (source != storage::ChangeSource::LOCAL) {
    return;
  }

  commits_to_upload_ = true;
  UploadUnsyncedCommits();
}

void PageSyncImpl::GetObject(
    storage::ObjectIdView object_id,
    std::function<void(storage::Status status, uint64_t size, mx::socket data)>
        callback) {
  GetAuthToken([ this, object_id = object_id.ToString(),
                 callback ](std::string auth_token) mutable {
    cloud_provider_->GetObject(
        auth_token, object_id,
        [ this, object_id, callback = std::move(callback) ](
            cloud_provider_firebase::Status status, uint64_t size,
            mx::socket data) mutable {
          if (status == cloud_provider_firebase::Status::NETWORK_ERROR) {
            FXL_LOG(WARNING)
                << log_prefix_
                << "GetObject() failed due to a connection error, retrying.";
            Retry([
              this, object_id = std::move(object_id),
              callback = std::move(callback)
            ] { GetObject(object_id, callback); });
            return;
          }

          backoff_->Reset();
          if (status != cloud_provider_firebase::Status::OK) {
            FXL_LOG(WARNING)
                << log_prefix_
                << "Fetching remote object failed with status: " << status;
            callback(storage::Status::IO_ERROR, 0, mx::socket());
            return;
          }

          callback(storage::Status::OK, size, std::move(data));
        });
  },
               [this, callback] {
                 FXL_LOG(ERROR)
                     << log_prefix_ << "Failed to retrieve the auth token, "
                     << "cannot download the object.";
                 callback(storage::Status::IO_ERROR, 0, mx::socket());
               });
}

void PageSyncImpl::OnRemoteCommits(
    std::vector<cloud_provider_firebase::Record> records) {
  if (batch_download_) {
    // If there is already a commit batch being downloaded, save the new commits
    // to be downloaded when it is done.
    std::move(records.begin(), records.end(),
              std::back_inserter(commits_to_download_));
    return;
  }
  SetDownloadState(REMOTE_COMMIT_DOWNLOAD);
  DownloadBatch(std::move(records), nullptr);
}

void PageSyncImpl::OnConnectionError() {
  FXL_DCHECK(remote_watch_set_);
  // Reset the watcher and schedule a retry.
  cloud_provider_->UnwatchCommits(this);
  remote_watch_set_ = false;
  FXL_LOG(WARNING)
      << log_prefix_
      << "Connection error in the remote commit watcher, retrying.";
  Retry([this] { SetRemoteWatcher(true); });
}

void PageSyncImpl::OnTokenExpired() {
  FXL_DCHECK(remote_watch_set_);
  // Reset the watcher and schedule a retry.
  cloud_provider_->UnwatchCommits(this);
  remote_watch_set_ = false;
  FXL_LOG(INFO) << log_prefix_ << "Firebase token expired, refreshing.";
  Retry([this] { SetRemoteWatcher(true); });
}

void PageSyncImpl::OnMalformedNotification() {
  SetDownloadState(DOWNLOAD_ERROR);
  HandleError("Received a malformed remote commit notification.");
}

void PageSyncImpl::StartDownload() {
  // Retrieve the server-side timestamp of the last commit we received.
  std::string last_commit_ts;
  auto status = storage_->GetSyncMetadata(kTimestampKey, &last_commit_ts);
  // NOT_FOUND means that we haven't persisted the state yet, e.g. because we
  // haven't received any remote commits yet. In this case an empty timestamp is
  // the right value.
  if (status != storage::Status::OK && status != storage::Status::NOT_FOUND) {
    SetDownloadState(DOWNLOAD_ERROR);
    HandleError("Failed to retrieve the sync metadata.");
    return;
  }
  if (last_commit_ts.empty()) {
    FXL_VLOG(1) << log_prefix_ << "starting sync for the first time, "
                << "retrieving all remote commits";
  } else {
    // TODO(ppi): print the timestamp out as human-readable wall time.
    FXL_VLOG(1) << log_prefix_ << "starting sync again, "
                << "retrieving commits uploaded after: " << last_commit_ts;
  }

  SetState(CATCH_UP_DOWNLOAD, WAIT_CATCH_UP_DOWNLOAD);

  GetAuthToken(
      [ this,
        last_commit_ts = std::move(last_commit_ts) ](std::string auth_token) {
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
                Retry([this] { StartDownload(); });
                return;
              }
              backoff_->Reset();

              if (records.empty()) {
                // If there is no remote commits to add, announce that we're
                // done.
                FXL_VLOG(1) << log_prefix_
                            << "initial sync finished, no new remote commits";
                BacklogDownloaded();
              } else {
                FXL_VLOG(1) << log_prefix_ << "retrieved " << records.size()
                            << " (possibly) new remote commits, "
                            << "adding them to storage.";
                // If not, fire the backlog download callback when the remote
                // commits are downloaded.
                const auto record_count = records.size();
                DownloadBatch(std::move(records), [this, record_count] {
                  FXL_VLOG(1) << log_prefix_ << "initial sync finished, added "
                              << record_count << " remote commits.";
                  BacklogDownloaded();
                });
              }
            });
      },
      [this] {
        HandleError(
            "Failed to retrieve the auth token to download commit backlog.");
      });
}

void PageSyncImpl::StartUpload() {
  if (!upload_enabled_ || !download_list_retrieved_) {
    // Only start uploading when the backlog is downloaded and upload is
    // enabled.
    CheckIdle();
    return;
  }

  // Prime the upload process.
  commits_to_upload_ = true;
  UploadUnsyncedCommits();
}

void PageSyncImpl::DownloadBatch(
    std::vector<cloud_provider_firebase::Record> records,
    fxl::Closure on_done) {
  FXL_DCHECK(!batch_download_);
  batch_download_ = std::make_unique<BatchDownload>(
      storage_, std::move(records), [ this, on_done = std::move(on_done) ] {
        if (on_done) {
          on_done();
        }
        batch_download_.reset();

        if (commits_to_download_.empty()) {
          SetDownloadState(DOWNLOAD_IDLE);
          UploadUnsyncedCommits();
          return;
        }
        auto commits = std::move(commits_to_download_);
        commits_to_download_.clear();
        DownloadBatch(std::move(commits), nullptr);
      },
      [this] {
        SetDownloadState(DOWNLOAD_ERROR);
        HandleError("Failed to persist a remote commit in storage");
      });
  batch_download_->Start();
}

void PageSyncImpl::SetRemoteWatcher(bool is_retry) {
  FXL_DCHECK(!remote_watch_set_);
  // Retrieve the server-side timestamp of the last commit we received.
  std::string last_commit_ts;
  auto status = storage_->GetSyncMetadata(kTimestampKey, &last_commit_ts);
  if (status != storage::Status::OK && status != storage::Status::NOT_FOUND) {
    download_state_ = DOWNLOAD_ERROR;
    HandleError("Failed to retrieve the sync metadata.");
    return;
  }

  GetAuthToken(
      [ this, is_retry,
        last_commit_ts = std::move(last_commit_ts) ](std::string auth_token) {
        cloud_provider_->WatchCommits(auth_token, last_commit_ts, this);
        remote_watch_set_ = true;
        if (is_retry) {
          FXL_LOG(INFO) << log_prefix_ << "Cloud watcher re-established";
        }
      },
      [this] {
        HandleError(
            "Failed to retrieve the auth token to set a cloud watcher.");
      });
}

void PageSyncImpl::UploadUnsyncedCommits() {
  if (!commits_to_upload_) {
    SetUploadState(UPLOAD_IDLE);
    CheckIdle();
    return;
  }

  if (batch_upload_) {
    // If we are already uploading a commit batch, return early.
    return;
  }

  // Retrieve the backlog of the existing unsynced commits and enqueue them for
  // upload.
  // TODO(ppi): either switch to a paginating API or (better?) ensure that long
  // backlogs of local commits are squashed in storage, as otherwise the list of
  // commits can be possibly very big.
  storage_->GetUnsyncedCommits(
      [this](storage::Status status,
             std::vector<std::unique_ptr<const storage::Commit>> commits) {
        if (status != storage::Status::OK) {
          SetUploadState(UPLOAD_ERROR);
          HandleError("Failed to retrieve the unsynced commits");
          return;
        }
        VerifyUnsyncedCommits(std::move(commits));

        if (!local_watch_set_) {
          // Subscribe to notifications about new commits in Storage.
          storage_->AddCommitWatcher(this);
          local_watch_set_ = true;
        }
      });
}

void PageSyncImpl::VerifyUnsyncedCommits(
    std::vector<std::unique_ptr<const storage::Commit>> commits) {
  // If we have no commit to upload, skip.
  if (commits.empty()) {
    SetUploadState(UPLOAD_IDLE);
    commits_to_upload_ = false;
    CheckIdle();
    return;
  }

  if (batch_download_) {
    // If a commit batch is currently being downloaded, don't try to start the
    // upload.
    SetUploadState(WAIT_REMOTE_DOWNLOAD);
    return;
  }

  SetUploadState(UPLOAD_PENDING);
  storage_->GetHeadCommitIds(fxl::MakeCopyable([
    this, commits = std::move(commits)
  ](storage::Status status, std::vector<storage::CommitId> heads) mutable {
    if (status != storage::Status::OK) {
      SetUploadState(UPLOAD_ERROR);
      HandleError("Failed to retrieve the current heads");
      return;
    }
    if (batch_upload_) {
      // If we are already uploading a commit batch, return early.
      return;
    }
    FXL_DCHECK(!heads.empty());

    if (heads.size() > 1u) {
      // Too many local heads.
      commits_to_upload_ = false;
      SetUploadState(WAIT_TOO_MANY_LOCAL_HEADS);
      CheckIdle();
      return;
    }

    SetUploadState(UPLOAD_IN_PROGRESS);
    HandleUnsyncedCommits(std::move(commits));
  }));
}

void PageSyncImpl::HandleUnsyncedCommits(
    std::vector<std::unique_ptr<const storage::Commit>> commits) {
  FXL_DCHECK(!batch_upload_);
  FXL_DCHECK(commits_to_upload_);
  batch_upload_ =
      std::make_unique<BatchUpload>(
          storage_, cloud_provider_, auth_provider_, std::move(commits),
          [this] {
            // Upload succeeded, reset the backoff delay.
            backoff_->Reset();
            batch_upload_.reset();
            UploadUnsyncedCommits();
          },
          [this] {
            FXL_LOG(WARNING)
                << log_prefix_
                << "commit upload failed due to a connection error, retrying.";
            SetUploadState(UPLOAD_PENDING);
            Retry([this] {
              batch_upload_.reset();
              UploadUnsyncedCommits();
            });
          });
  batch_upload_->Start();
}

void PageSyncImpl::HandleError(const char error_description[]) {
  FXL_LOG(ERROR) << log_prefix_ << error_description << " Stopping sync.";
  if (local_watch_set_) {
    storage_->RemoveCommitWatcher(this);
  }
  if (remote_watch_set_) {
    cloud_provider_->UnwatchCommits(this);
  }
  storage_->SetSyncDelegate(nullptr);
  on_error_();
  errored_ = true;
}

void PageSyncImpl::CheckIdle() {
  if (IsIdle()) {
    if (on_idle_) {
      on_idle_();
    }
  }
}

void PageSyncImpl::BacklogDownloaded() {
  download_list_retrieved_ = true;
  SetDownloadState(DOWNLOAD_IDLE);
  if (on_backlog_downloaded_) {
    on_backlog_downloaded_();
  }
  SetRemoteWatcher(false);
  StartUpload();
}

void PageSyncImpl::Retry(fxl::Closure callable) {
  task_runner_->PostDelayedTask(
      [
        weak_this = weak_factory_.GetWeakPtr(), callable = std::move(callable)
      ]() {
        if (weak_this && !weak_this->errored_) {
          callable();
        }
      },
      backoff_->GetNext());
}

void PageSyncImpl::NotifyStateWatcher() {
  if (ledger_watcher_) {
    ledger_watcher_->Notify(download_state_, upload_state_);
  }
  if (page_watcher_) {
    page_watcher_->Notify(download_state_, upload_state_);
  }
}

void PageSyncImpl::SetDownloadState(DownloadSyncState sync_state) {
  download_state_ = sync_state;
  NotifyStateWatcher();
}

void PageSyncImpl::SetUploadState(UploadSyncState sync_state) {
  upload_state_ = sync_state;
  NotifyStateWatcher();
}
void PageSyncImpl::SetState(DownloadSyncState download_state,
                            UploadSyncState upload_state) {
  download_state_ = download_state;
  if (upload_enabled_)
    upload_state_ = upload_state;
  NotifyStateWatcher();
}

void PageSyncImpl::GetAuthToken(std::function<void(std::string)> on_token_ready,
                                fxl::Closure on_failed) {
  auto request = auth_provider_->GetFirebaseToken([
    on_token_ready = std::move(on_token_ready), on_failed = std::move(on_failed)
  ](auth_provider::AuthStatus auth_status, std::string auth_token) {
    if (auth_status != auth_provider::AuthStatus::OK) {
      on_failed();
      return;
    }
    on_token_ready(std::move(auth_token));
  });
  auth_token_requests_.emplace(request);
}

}  // namespace cloud_sync
