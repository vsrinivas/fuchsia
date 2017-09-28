// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cloud_sync/impl/batch_upload.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

#include <trace/event.h>

#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/logging.h"
#include "peridot/bin/ledger/callback/scoped_callback.h"
#include "peridot/bin/ledger/callback/trace_callback.h"
#include "peridot/bin/ledger/callback/waiter.h"
#include "peridot/bin/ledger/cloud_provider/public/commit.h"
#include "peridot/bin/ledger/cloud_provider/public/types.h"

namespace cloud_sync {

BatchUpload::BatchUpload(
    storage::PageStorage* storage,
    cloud_provider_firebase::PageCloudHandler* cloud_provider,
    auth_provider::AuthProvider* auth_provider,
    std::vector<std::unique_ptr<const storage::Commit>> commits,
    fxl::Closure on_done,
    fxl::Closure on_error,
    unsigned int max_concurrent_uploads)
    : storage_(storage),
      cloud_provider_(cloud_provider),
      auth_provider_(auth_provider),
      commits_(std::move(commits)),
      on_done_(std::move(on_done)),
      on_error_(std::move(on_error)),
      max_concurrent_uploads_(max_concurrent_uploads),
      weak_ptr_factory_(this) {
  TRACE_ASYNC_BEGIN("ledger", "batch_upload",
                    reinterpret_cast<uintptr_t>(this));
  FXL_DCHECK(storage_);
  FXL_DCHECK(cloud_provider_);
  FXL_DCHECK(auth_provider_);
}

BatchUpload::~BatchUpload() {
  TRACE_ASYNC_END("ledger", "batch_upload", reinterpret_cast<uintptr_t>(this));
}

void BatchUpload::Start() {
  FXL_DCHECK(!started_);
  FXL_DCHECK(!errored_);
  started_ = true;
  RefreshAuthToken([this] {
    storage_->GetUnsyncedPieces(callback::MakeScoped(
        weak_ptr_factory_.GetWeakPtr(),
        [this](storage::Status status,
               std::vector<storage::ObjectId> object_ids) {
          FXL_DCHECK(status == storage::Status::OK);
          for (auto& object_id : object_ids) {
            remaining_object_ids_.push(std::move(object_id));
          }
          StartObjectUpload();
        }));
  });
}

void BatchUpload::Retry() {
  FXL_DCHECK(started_);
  FXL_DCHECK(errored_);
  errored_ = false;
  RefreshAuthToken([this] { StartObjectUpload(); });
}

void BatchUpload::StartObjectUpload() {
  FXL_DCHECK(current_uploads_ == 0u);
  // If there are no unsynced objects left, upload the commits.
  if (remaining_object_ids_.empty()) {
    FilterAndUploadCommits();
    return;
  }

  while (current_uploads_ < max_concurrent_uploads_ &&
         !remaining_object_ids_.empty()) {
    UploadNextObject();
  }
}

void BatchUpload::UploadNextObject() {
  FXL_DCHECK(!remaining_object_ids_.empty());
  FXL_DCHECK(current_uploads_ < max_concurrent_uploads_);
  current_uploads_++;
  auto object_id_to_send = std::move(remaining_object_ids_.front());
  // Pop the object from the queue - if the upload fails, we will re-enqueue it.
  remaining_object_ids_.pop();
  storage_->GetPiece(object_id_to_send,
                     callback::MakeScoped(
                         weak_ptr_factory_.GetWeakPtr(),
                         [this](storage::Status storage_status,
                                std::unique_ptr<const storage::Object> object) {
                           FXL_DCHECK(storage_status == storage::Status::OK);
                           UploadObject(std::move(object));
                         }));
}

void BatchUpload::UploadObject(std::unique_ptr<const storage::Object> object) {
  zx::vmo data;
  auto status = object->GetVmo(&data);
  // TODO(ppi): LE-225 Handle disk IO errors.
  FXL_DCHECK(status == storage::Status::OK);

  storage::ObjectId id = object->GetId();
  cloud_provider_->AddObject(auth_token_, object->GetId(), std::move(data), [
    this, id = std::move(id)
  ](cloud_provider_firebase::Status status) mutable {
    FXL_DCHECK(current_uploads_ > 0);
    current_uploads_--;

    if (status != cloud_provider_firebase::Status::OK) {
      errored_ = true;
      // Re-enqueue the object for another upload attempt.
      remaining_object_ids_.push(std::move(id));

      if (current_uploads_ == 0u) {
        on_error_();
      }
      return;
    }

    // Uploading the object succeeded.
    storage_->MarkPieceSynced(
        id, callback::MakeScoped(
                weak_ptr_factory_.GetWeakPtr(), [this](storage::Status status) {
                  FXL_DCHECK(status == storage::Status::OK);

                  // Notify the user about the error once all pending uploads of
                  // the recent retry complete.
                  if (errored_ && current_uploads_ == 0u) {
                    on_error_();
                    return;
                  }

                  if (current_uploads_ == 0 && remaining_object_ids_.empty()) {
                    // All the referenced objects are uploaded, upload the
                    // commits.
                    FilterAndUploadCommits();
                    return;
                  }

                  if (!errored_ && !remaining_object_ids_.empty()) {
                    UploadNextObject();
                  }
                }));
  });
}

void BatchUpload::FilterAndUploadCommits() {
  // Remove all commits that have been synced since this upload object was
  // created. This will happen if a merge is executed on multiple devices at the
  // same time.
  storage_->GetUnsyncedCommits(callback::MakeScoped(
      weak_ptr_factory_.GetWeakPtr(),
      [this](storage::Status status,
             std::vector<std::unique_ptr<const storage::Commit>> commits) {
        std::unordered_set<storage::CommitId> commit_ids;
        commit_ids.reserve(commits.size());
        std::transform(
            commits.begin(), commits.end(),
            std::inserter(commit_ids, commit_ids.begin()),
            [](const std::unique_ptr<const storage::Commit>& commit) {
              return commit->GetId();
            });

        commits_.erase(
            std::remove_if(
                commits_.begin(), commits_.end(),
                [&commit_ids](
                    const std::unique_ptr<const storage::Commit>& commit) {
                  return commit_ids.count(commit->GetId()) == 0;
                }),
            commits_.end());
        UploadCommits();
      }));
}

void BatchUpload::UploadCommits() {
  FXL_DCHECK(!errored_);
  std::vector<cloud_provider_firebase::Commit> commits;
  std::vector<storage::CommitId> ids;
  for (auto& storage_commit : commits_) {
    storage::CommitId id = storage_commit->GetId();
    cloud_provider_firebase::Commit commit(
        id, storage_commit->GetStorageBytes().ToString());
    commits.push_back(std::move(commit));
    ids.push_back(std::move(id));
  }
  cloud_provider_->AddCommits(auth_token_, std::move(commits), [
    this, commit_ids = std::move(ids)
  ](cloud_provider_firebase::Status status) {
    // UploadCommit() is called as a last step of a so-far-successful upload
    // attempt, so we couldn't have failed before.
    FXL_DCHECK(!errored_);
    if (status != cloud_provider_firebase::Status::OK) {
      errored_ = true;
      on_error_();
      return;
    }
    auto waiter =
        callback::StatusWaiter<storage::Status>::Create(storage::Status::OK);

    for (auto& id : commit_ids) {
      storage_->MarkCommitSynced(id, waiter->NewCallback());
    }
    waiter->Finalize(callback::MakeScoped(
        weak_ptr_factory_.GetWeakPtr(), [this](storage::Status status) {
          // TODO(nellyv): Handle IO errors. See LE-225.
          FXL_DCHECK(status == storage::Status::OK);
          // This object can be deleted in the on_done_() callback, don't do
          // anything after the call.
          on_done_();
        }));
  });
}

void BatchUpload::RefreshAuthToken(fxl::Closure on_refreshed) {
  auto traced_callback = TRACE_CALLBACK(std::move(on_refreshed), "ledger",
                                        "batch_upload_refresh_auth_token");
  auth_token_requests_.emplace(auth_provider_->GetFirebaseToken([
    this, on_refreshed = std::move(traced_callback)
  ](auth_provider::AuthStatus auth_status, std::string auth_token) {
    if (auth_status != auth_provider::AuthStatus::OK) {
      FXL_LOG(ERROR) << "Failed to retrieve the auth token for upload.";
      errored_ = true;
      on_error_();
      return;
    }

    auth_token_ = std::move(auth_token);
    on_refreshed();
  }));
}

}  // namespace cloud_sync
