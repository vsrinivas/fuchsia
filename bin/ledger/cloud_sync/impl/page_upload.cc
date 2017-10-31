// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cloud_sync/impl/page_upload.h"

#include "lib/fxl/functional/make_copyable.h"
#include "peridot/bin/ledger/callback/scoped_callback.h"

namespace cloud_sync {
PageUpload::PageUpload(callback::ScopedTaskRunner* task_runner,
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
      log_prefix_("Page " + convert::ToHex(storage->GetId()) +
                  " upload sync: "),
      backoff_(std::move(backoff)),
      weak_ptr_factory_(this) {}

PageUpload::~PageUpload() {}

void PageUpload::StartUpload() {
  // Prime the upload process.
  if (state_ == UPLOAD_STOPPED) {
    commits_to_upload_ = true;
    SetState(UPLOAD_SETUP);
  }
  UploadUnsyncedCommits();
}

void PageUpload::OnNewCommits(
    const std::vector<std::unique_ptr<const storage::Commit>>& /*commits*/,
    storage::ChangeSource source) {
  // Only upload the locally created commits.
  // TODO(ppi): revisit this when we have p2p sync, too.
  if (source != storage::ChangeSource::LOCAL) {
    return;
  }

  commits_to_upload_ = true;
  if (!delegate_->IsDownloadIdle()) {
    // If a commit batch is currently being downloaded, don't try to start the
    // upload.
    SetState(UPLOAD_WAIT_REMOTE_DOWNLOAD);
  } else if (state_ == UPLOAD_TEMPORARY_ERROR) {
    // Upload is already scheduled to retry uploading. No need to do anything
    // here.
  } else {
    SetState(UPLOAD_PENDING);
    UploadUnsyncedCommits();
  }
}

void PageUpload::UploadUnsyncedCommits() {
  if (!commits_to_upload_) {
    SetState(UPLOAD_IDLE);
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
  storage_->GetUnsyncedCommits(callback::MakeScoped(
      weak_ptr_factory_.GetWeakPtr(),
      [this](storage::Status status,
             std::vector<std::unique_ptr<const storage::Commit>> commits) {
        if (status != storage::Status::OK) {
          SetState(UPLOAD_PERMANENT_ERROR);
          HandleError("Failed to retrieve the unsynced commits");
          return;
        }

        if (state_ == UPLOAD_SETUP) {
          // Subscribe to notifications about new commits in Storage.
          storage_->AddCommitWatcher(this);
        }

        VerifyUnsyncedCommits(std::move(commits));
      }));
}

void PageUpload::VerifyUnsyncedCommits(
    std::vector<std::unique_ptr<const storage::Commit>> commits) {
  // If we have no commit to upload, skip.
  if (commits.empty()) {
    SetState(UPLOAD_IDLE);
    commits_to_upload_ = false;
    return;
  }

  storage_->GetHeadCommitIds(callback::MakeScoped(
      weak_ptr_factory_.GetWeakPtr(),
      fxl::MakeCopyable([this, commits = std::move(commits)](
                            storage::Status status,
                            std::vector<storage::CommitId> heads) mutable {
        if (status != storage::Status::OK) {
          HandleError("Failed to retrieve the current heads");
          return;
        }
        if (batch_upload_) {
          // If we are already uploading a commit batch, return early.
          return;
        }
        FXL_DCHECK(!heads.empty());

        if (!delegate_->IsDownloadIdle()) {
          // If a commit batch is currently being downloaded, don't try to start
          // the upload.
          SetState(UPLOAD_WAIT_REMOTE_DOWNLOAD);
          return;
        }

        if (heads.size() > 1u) {
          // Too many local heads.
          commits_to_upload_ = false;
          SetState(UPLOAD_WAIT_TOO_MANY_LOCAL_HEADS);
          return;
        }

        HandleUnsyncedCommits(std::move(commits));
      })));
}

void PageUpload::HandleUnsyncedCommits(
    std::vector<std::unique_ptr<const storage::Commit>> commits) {
  FXL_DCHECK(!batch_upload_);
  FXL_DCHECK(commits_to_upload_);
  SetState(UPLOAD_IN_PROGRESS);
  batch_upload_ = std::make_unique<BatchUpload>(
      storage_, encryption_service_, page_cloud_, std::move(commits),
      [this] {
        // Upload succeeded, reset the backoff delay.
        backoff_->Reset();
        batch_upload_.reset();
        UploadUnsyncedCommits();
      },
      [this](BatchUpload::ErrorType error_type) {
        switch (error_type) {
          case BatchUpload::ErrorType::TEMPORARY: {
            FXL_LOG(WARNING)
                << log_prefix_
                << "commit upload failed due to a connection error, retrying.";
            SetState(UPLOAD_TEMPORARY_ERROR);
            batch_upload_.reset();
            RetryWithBackoff([this] { UploadUnsyncedCommits(); });
          } break;
          case BatchUpload::ErrorType::PERMANENT: {
            FXL_LOG(WARNING) << log_prefix_
                             << "commit upload failed with a permanent error.";
            SetState(UPLOAD_PERMANENT_ERROR);
          } break;
        }
      });
  batch_upload_->Start();
}

void PageUpload::HandleError(const char error_description[]) {
  FXL_LOG(ERROR) << log_prefix_ << error_description << " Stopping sync.";
  if (state_ > UPLOAD_SETUP) {
    storage_->RemoveCommitWatcher(this);
  }
  SetState(UPLOAD_PERMANENT_ERROR);
}

void PageUpload::RetryWithBackoff(fxl::Closure callable) {
  task_runner_->PostDelayedTask(
      [this, callable = std::move(callable)]() {
        if (this->state_ != UPLOAD_PERMANENT_ERROR) {
          callable();
        }
      },
      backoff_->GetNext());
}

void PageUpload::SetState(UploadSyncState new_state) {
  if (new_state == state_) {
    return;
  }
  state_ = new_state;
  delegate_->SetUploadState(state_);
}

bool PageUpload::IsIdle() {
  switch (state_) {
    case UPLOAD_STOPPED:
    case UPLOAD_IDLE:
    case UPLOAD_WAIT_TOO_MANY_LOCAL_HEADS:
    case UPLOAD_PERMANENT_ERROR:
      return true;
      break;
    case UPLOAD_SETUP:
    case UPLOAD_PENDING:
    case UPLOAD_WAIT_REMOTE_DOWNLOAD:
    case UPLOAD_TEMPORARY_ERROR:
    case UPLOAD_IN_PROGRESS:
      return false;
      break;
  }
}

}  // namespace cloud_sync
