// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cloud_sync/impl/page_download.h"

#include <lib/fit/function.h>
#include <lib/fxl/strings/concatenate.h>

#include "peridot/bin/ledger/cloud_sync/impl/constants.h"
#include "peridot/bin/ledger/storage/public/data_source.h"
#include "peridot/bin/ledger/storage/public/read_data_source.h"

namespace cloud_sync {
namespace {
DownloadSyncState GetMergedState(DownloadSyncState commit_state,
                                 int current_get_object_calls) {
  if (commit_state != DOWNLOAD_IDLE) {
    return commit_state;
  }
  return current_get_object_calls == 0 ? DOWNLOAD_IDLE : DOWNLOAD_IN_PROGRESS;
}

bool IsPermanentError(cloud_provider::Status status) {
  switch (status) {
    case cloud_provider::Status::OK:
    case cloud_provider::Status::AUTH_ERROR:
    case cloud_provider::Status::NETWORK_ERROR:
      return false;
    case cloud_provider::Status::ARGUMENT_ERROR:
    case cloud_provider::Status::INTERNAL_ERROR:
    case cloud_provider::Status::NOT_FOUND:
    case cloud_provider::Status::PARSE_ERROR:
    case cloud_provider::Status::SERVER_ERROR:
    case cloud_provider::Status::UNKNOWN_ERROR:
      return true;
  }
}
}  // namespace

PageDownload::PageDownload(callback::ScopedTaskRunner* task_runner,
                           storage::PageStorage* storage,
                           storage::PageSyncClient* sync_client,
                           encryption::EncryptionService* encryption_service,
                           cloud_provider::PageCloudPtr* page_cloud,
                           Delegate* delegate,
                           std::unique_ptr<backoff::Backoff> backoff)
    : task_runner_(task_runner),
      storage_(storage),
      sync_client_(sync_client),
      encryption_service_(encryption_service),
      page_cloud_(page_cloud),
      delegate_(delegate),
      backoff_(std::move(backoff)),
      log_prefix_(fxl::Concatenate(
          {"Page ", convert::ToHex(storage->GetId()), " download sync: "})),
      watcher_binding_(this) {}

PageDownload::~PageDownload() { sync_client_->SetSyncDelegate(nullptr); }

void PageDownload::StartDownload() {
  SetCommitState(DOWNLOAD_BACKLOG);

  sync_client_->SetSyncDelegate(this);

  // Retrieve the server-side timestamp of the last commit we received.
  storage_->GetSyncMetadata(
      kTimestampKey,
      task_runner_->MakeScoped([this](storage::Status status,
                                      std::string last_commit_token_id) {
        // NOT_FOUND means that we haven't persisted the state yet, e.g. because
        // we haven't received any remote commits yet. In this case an empty
        // timestamp is the right value.
        if (status != storage::Status::OK &&
            status != storage::Status::NOT_FOUND) {
          HandleDownloadCommitError("Failed to retrieve the sync metadata.");
          return;
        }
        if (last_commit_token_id.empty()) {
          FXL_VLOG(1) << log_prefix_ << "starting sync for the first time, "
                      << "retrieving all remote commits";
        } else {
          // TODO(ppi): print the timestamp out as human-readable wall time.
          FXL_VLOG(1) << log_prefix_ << "starting sync again, "
                      << "retrieving commits uploaded after: "
                      << last_commit_token_id;
        }

        std::unique_ptr<cloud_provider::Token> position_token;
        if (!last_commit_token_id.empty()) {
          position_token = std::make_unique<cloud_provider::Token>();
          position_token->opaque_id = convert::ToArray(last_commit_token_id);
        }
        // TODO(ppi): handle pagination when the response is huge.
        (*page_cloud_)
            ->GetCommits(
                std::move(position_token),
                [this](cloud_provider::Status cloud_status,
                       fidl::VectorPtr<cloud_provider::Commit> commits,
                       std::unique_ptr<cloud_provider::Token> position_token) {
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

                  if (commits->empty()) {
                    // If there is no remote commits to add, announce that
                    // we're done.
                    FXL_VLOG(1)
                        << log_prefix_
                        << "initial sync finished, no new remote commits";
                    BacklogDownloaded();
                  } else {
                    FXL_VLOG(1)
                        << log_prefix_ << "retrieved " << commits->size()
                        << " (possibly) new remote commits, "
                        << "adding them to storage.";
                    // If not, fire the backlog download callback when the
                    // remote commits are downloaded.
                    const auto commit_count = commits->size();
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

bool PageDownload::IsIdle() {
  switch (GetMergedState(commit_state_, current_get_object_calls_)) {
    case DOWNLOAD_NOT_STARTED:
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

void PageDownload::BacklogDownloaded() { SetRemoteWatcher(false); }

void PageDownload::SetRemoteWatcher(bool is_retry) {
  FXL_DCHECK(commit_state_ == DOWNLOAD_BACKLOG ||
             commit_state_ == DOWNLOAD_TEMPORARY_ERROR)
      << "Current state: " << commit_state_;
  SetCommitState(DOWNLOAD_SETTING_REMOTE_WATCHER);
  // Retrieve the server-side token of the last commit we received.
  std::string last_commit_token_id;
  storage_->GetSyncMetadata(
      kTimestampKey,
      task_runner_->MakeScoped([this, is_retry](
                                   storage::Status status,
                                   std::string last_commit_token_id) {
        if (status != storage::Status::OK &&
            status != storage::Status::NOT_FOUND) {
          HandleDownloadCommitError("Failed to retrieve the sync metadata.");
          return;
        }

        std::unique_ptr<cloud_provider::Token> position_token;
        if (!last_commit_token_id.empty()) {
          position_token = std::make_unique<cloud_provider::Token>();
          position_token->opaque_id = convert::ToArray(last_commit_token_id);
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
                    HandleDownloadCommitError(
                        "Unexpected error when setting the PageCloudWatcher.");
                  }
                });
        SetCommitState(DOWNLOAD_IDLE);
        if (is_retry) {
          FXL_LOG(INFO) << log_prefix_ << "Cloud watcher re-established";
        }
      }));
}

void PageDownload::OnNewCommits(
    fidl::VectorPtr<cloud_provider::Commit> commits,
    std::unique_ptr<cloud_provider::Token> position_token,
    OnNewCommitsCallback callback) {
  if (batch_download_) {
    // If there is already a commit batch being downloaded, save the new commits
    // to be downloaded when it is done.
    for (auto& commit : *commits) {
      commits_to_download_.push_back(std::move(commit));
    }
    position_token_ = std::move(position_token);
    callback();
    return;
  }
  SetCommitState(DOWNLOAD_IN_PROGRESS);
  DownloadBatch(std::move(commits), std::move(position_token),
                std::move(callback));
}

void PageDownload::OnNewObject(fidl::VectorPtr<uint8_t> /*id*/,
                               fuchsia::mem::Buffer /*data*/,
                               OnNewObjectCallback /*callback*/) {
  // No known cloud provider implementations use this method.
  // TODO(ppi): implement this method when we have such cloud provider
  // implementations.
  FXL_NOTIMPLEMENTED();
}

void PageDownload::OnError(cloud_provider::Status status) {
  FXL_DCHECK(commit_state_ == DOWNLOAD_IDLE ||
             commit_state_ == DOWNLOAD_IN_PROGRESS);
  if (!IsPermanentError(status)) {
    // Reset the watcher and schedule a retry.
    if (watcher_binding_.is_bound()) {
      watcher_binding_.Unbind();
    }
    SetCommitState(DOWNLOAD_TEMPORARY_ERROR);
    FXL_LOG(WARNING)
        << log_prefix_
        << "Connection error in the remote commit watcher, retrying.";
    RetryWithBackoff([this] { SetRemoteWatcher(true); });
    return;
  }

  if (status == cloud_provider::Status::PARSE_ERROR) {
    HandleDownloadCommitError(
        "Received a malformed remote commit notification.");
    return;
  }

  FXL_LOG(WARNING) << "Received unexpected error from PageCloudWatcher: "
                   << status;
  HandleDownloadCommitError("Received unexpected error from PageCloudWatcher.");
}

void PageDownload::DownloadBatch(
    fidl::VectorPtr<cloud_provider::Commit> commits,
    std::unique_ptr<cloud_provider::Token> position_token,
    fit::closure on_done) {
  FXL_DCHECK(!batch_download_);
  batch_download_ = std::make_unique<BatchDownload>(
      storage_, encryption_service_, std::move(commits),
      std::move(position_token),
      [this, on_done = std::move(on_done)] {
        if (on_done) {
          on_done();
        }
        batch_download_.reset();

        if (commits_to_download_->empty()) {
          // Don't set to idle if we're in process of setting the remote
          // watcher.
          if (commit_state_ == DOWNLOAD_IN_PROGRESS) {
            SetCommitState(DOWNLOAD_IDLE);
          }
          return;
        }
        auto commits = std::move(commits_to_download_);
        commits_to_download_ = fidl::VectorPtr<cloud_provider::Commit>::New(0);
        DownloadBatch(std::move(commits), std::move(position_token_), nullptr);
      },
      [this] {
        HandleDownloadCommitError(
            "Failed to persist a remote commit in storage");
      });
  batch_download_->Start();
}

void PageDownload::GetObject(
    storage::ObjectIdentifier object_identifier,
    fit::function<void(storage::Status, storage::ChangeSource,
                       storage::IsObjectSynced,
                       std::unique_ptr<storage::DataSource::DataChunk>)>
        callback) {
  current_get_object_calls_++;
  UpdateDownloadState();
  encryption_service_->GetObjectName(
      object_identifier,
      task_runner_->MakeScoped(
          [this, object_identifier, callback = std::move(callback)](
              encryption::Status status, std::string object_name) mutable {
            if (status != encryption::Status::OK) {
              HandleGetObjectError(std::move(object_identifier),
                                   encryption::IsPermanentError(status),
                                   "encryption", std::move(callback));
              return;
            }
            (*page_cloud_)
                ->GetObject(
                    convert::ToArray(object_name),
                    [this, object_identifier = std::move(object_identifier),
                     callback = std::move(callback)](
                        cloud_provider::Status status, uint64_t size,
                        zx::socket data) mutable {
                      if (status != cloud_provider::Status::OK) {
                        HandleGetObjectError(std::move(object_identifier),
                                             IsPermanentError(status),
                                             "cloud provider",
                                             std::move(callback));
                        return;
                      }

                      DecryptObject(
                          std::move(object_identifier),
                          storage::DataSource::Create(std::move(data), size),
                          std::move(callback));
                    });
          }));
}

void PageDownload::DecryptObject(
    storage::ObjectIdentifier object_identifier,
    std::unique_ptr<storage::DataSource> content,
    fit::function<void(storage::Status, storage::ChangeSource,
                       storage::IsObjectSynced,
                       std::unique_ptr<storage::DataSource::DataChunk>)>
        callback) {
  storage::ReadDataSource(
      &managed_container_, std::move(content),
      [this, object_identifier = std::move(object_identifier),
       callback = std::move(callback)](
          storage::Status status,
          std::unique_ptr<storage::DataSource::DataChunk> content) mutable {
        if (status != storage::Status::OK) {
          HandleGetObjectError(std::move(object_identifier), true, "io",
                               std::move(callback));
          return;
        }
        encryption_service_->DecryptObject(
            object_identifier, content->Get().ToString(),
            [this, object_identifier, callback = std::move(callback)](
                encryption::Status status, std::string content) mutable {
              if (status != encryption::Status::OK) {
                HandleGetObjectError(object_identifier,
                                     encryption::IsPermanentError(status),
                                     "encryption", std::move(callback));
                return;
              }
              backoff_->Reset();
              callback(
                  storage::Status::OK, storage::ChangeSource::CLOUD,
                  storage::IsObjectSynced::YES,
                  storage::DataSource::DataChunk::Create(std::move(content)));
              current_get_object_calls_--;
              UpdateDownloadState();
            });
      });
}

void PageDownload::HandleGetObjectError(
    storage::ObjectIdentifier object_identifier, bool is_permanent,
    const char error_name[],
    fit::function<void(storage::Status, storage::ChangeSource,
                       storage::IsObjectSynced,
                       std::unique_ptr<storage::DataSource::DataChunk>)>
        callback) {
  if (is_permanent) {
    backoff_->Reset();
    FXL_LOG(WARNING) << log_prefix_ << "GetObject() failed due to a permanent "
                     << error_name << " error";
    callback(storage::Status::IO_ERROR, storage::ChangeSource::CLOUD,
             storage::IsObjectSynced::YES, nullptr);
    current_get_object_calls_--;
    UpdateDownloadState();
    return;
  }
  FXL_LOG(WARNING) << log_prefix_ << "GetObject() failed due to a "
                   << error_name << " error, retrying";
  current_get_object_calls_--;
  UpdateDownloadState();
  RetryWithBackoff([this, object_identifier = std::move(object_identifier),
                    callback = std::move(callback)]() mutable {
    GetObject(object_identifier, std::move(callback));
  });
}

void PageDownload::HandleDownloadCommitError(const char error_description[]) {
  FXL_LOG(ERROR) << log_prefix_ << error_description << " Stopping sync.";
  if (watcher_binding_.is_bound()) {
    watcher_binding_.Unbind();
  }
  sync_client_->SetSyncDelegate(nullptr);
  SetCommitState(DOWNLOAD_PERMANENT_ERROR);
}

void PageDownload::SetCommitState(DownloadSyncState new_state) {
  if (new_state == commit_state_) {
    return;
  }

  commit_state_ = new_state;
  UpdateDownloadState();
}

void PageDownload::UpdateDownloadState() {
  DownloadSyncState new_state =
      GetMergedState(commit_state_, current_get_object_calls_);

  // Notify only if the externally visible state changed.
  if (new_state != merged_state_) {
    merged_state_ = new_state;
    delegate_->SetDownloadState(
        GetMergedState(commit_state_, current_get_object_calls_));
  }
}

void PageDownload::RetryWithBackoff(fit::closure callable) {
  task_runner_->PostDelayedTask(
      [this, callable = std::move(callable)]() {
        if (this->commit_state_ != DOWNLOAD_PERMANENT_ERROR) {
          callable();
        }
      },
      backoff_->GetNext());
}

}  // namespace cloud_sync
