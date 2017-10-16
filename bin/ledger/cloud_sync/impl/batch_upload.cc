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
    encryption::EncryptionService* encryption_service,
    cloud_provider_firebase::PageCloudHandler* cloud_provider,
    auth_provider::AuthProvider* auth_provider,
    std::vector<std::unique_ptr<const storage::Commit>> commits,
    fxl::Closure on_done,
    std::function<void(ErrorType)> on_error,
    unsigned int max_concurrent_uploads)
    : storage_(storage),
      encryption_service_(encryption_service),
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
               std::vector<storage::ObjectDigest> object_digests) {
          if (status != storage::Status::OK) {
            errored_ = true;
            on_error_(ErrorType::PERMANENT);
            return;
          }
          remaining_object_digests_ = std::move(object_digests);
          StartObjectUpload();
        }));
  });
}

void BatchUpload::Retry() {
  FXL_DCHECK(started_);
  FXL_DCHECK(errored_);
  errored_ = false;
  error_type_ = ErrorType::TEMPORARY;
  RefreshAuthToken([this] { StartObjectUpload(); });
}

void BatchUpload::StartObjectUpload() {
  FXL_DCHECK(current_uploads_ == 0u);
  // If there are no unsynced objects left, upload the commits.
  if (remaining_object_digests_.empty()) {
    FilterAndUploadCommits();
    return;
  }

  while (current_uploads_ < max_concurrent_uploads_ &&
         !remaining_object_digests_.empty()) {
    UploadNextObject();
  }
}

void BatchUpload::UploadNextObject() {
  FXL_DCHECK(!remaining_object_digests_.empty());
  FXL_DCHECK(current_uploads_ < max_concurrent_uploads_);
  current_uploads_++;
  current_objects_handled_++;
  auto object_digest_to_send = std::move(remaining_object_digests_.back());
  // Pop the object from the queue - if the upload fails, we will re-enqueue it.
  remaining_object_digests_.pop_back();
  storage_->GetPiece(object_digest_to_send,
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

  storage::ObjectDigest digest = object->GetDigest();
  cloud_provider_->AddObject(
      auth_token_, object->GetDigest(), std::move(data),
      callback::MakeScoped(weak_ptr_factory_.GetWeakPtr(), [
        this, digest = std::move(digest)
      ](cloud_provider_firebase::Status status) {
        FXL_DCHECK(current_uploads_ > 0);
        current_uploads_--;

        if (status != cloud_provider_firebase::Status::OK) {
          FXL_DCHECK(current_objects_handled_ > 0);
          current_objects_handled_--;

          errored_ = true;
          // Re-enqueue the object for another upload attempt.
          remaining_object_digests_.push_back(std::move(digest));

          if (current_objects_handled_ == 0u) {
            on_error_(ErrorType::PERMANENT);
          }
          return;
        }

        // Uploading the object succeeded.
        storage_->MarkPieceSynced(
            digest,
            callback::MakeScoped(
                weak_ptr_factory_.GetWeakPtr(), [this](storage::Status status) {
                  FXL_DCHECK(current_objects_handled_ > 0);
                  current_objects_handled_--;

                  if (status != storage::Status::OK) {
                    errored_ = true;
                    error_type_ = ErrorType::PERMANENT;
                  }

                  // Notify the user about the error once all pending operations
                  // of the recent retry complete.
                  if (errored_ && current_objects_handled_ == 0u) {
                    on_error_(error_type_);
                    return;
                  }

                  if (current_objects_handled_ == 0 &&
                      remaining_object_digests_.empty()) {
                    // All the referenced objects are uploaded and marked as
                    // synced, upload the commits.
                    FilterAndUploadCommits();
                    return;
                  }

                  if (!errored_ && !remaining_object_digests_.empty()) {
                    UploadNextObject();
                  }
                }));
      }));
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

        if (commits_.empty()) {
          // Return early, all commits are synced.
          on_done_();
          return;
        }
        UploadCommits();
      }));
}

void BatchUpload::UploadCommits() {
  FXL_DCHECK(!errored_);
  std::vector<storage::CommitId> ids;
  auto waiter =
      callback::Waiter<encryption::Status, cloud_provider_firebase::Commit>::
          Create(encryption::Status::OK);
  for (auto& storage_commit : commits_) {
    storage::CommitId id = storage_commit->GetId();
    encryption_service_->EncryptCommit(
        storage_commit->GetStorageBytes(),
        [id, callback = waiter->NewCallback()](
            encryption::Status status,
            std::string encrypted_storage_bytes) mutable {
          callback(status,
                   cloud_provider_firebase::Commit(
                       std::move(id), std::move(encrypted_storage_bytes)));
        });
    ids.push_back(std::move(id));
  }
  waiter->Finalize(callback::MakeScoped(
      weak_ptr_factory_.GetWeakPtr(),
      [ this, ids = std::move(ids) ](
          encryption::Status status,
          std::vector<cloud_provider_firebase::Commit> commits) mutable {
        if (status != encryption::Status::OK) {
          errored_ = true;
          on_error_(ErrorType::PERMANENT);
          return;
        }
        cloud_provider_->AddCommits(
            auth_token_, std::move(commits),
            callback::MakeScoped(weak_ptr_factory_.GetWeakPtr(), [
              this, commit_ids = std::move(ids)
            ](cloud_provider_firebase::Status status) {
              // UploadCommit() is called as a last step of a so-far-successful
              // upload attempt, so we couldn't have failed before.
              FXL_DCHECK(!errored_);
              if (status != cloud_provider_firebase::Status::OK) {
                errored_ = true;
                on_error_(ErrorType::TEMPORARY);
                return;
              }
              auto waiter = callback::StatusWaiter<storage::Status>::Create(
                  storage::Status::OK);

              for (auto& id : commit_ids) {
                storage_->MarkCommitSynced(id, waiter->NewCallback());
              }
              waiter->Finalize(
                  callback::MakeScoped(weak_ptr_factory_.GetWeakPtr(),
                                       [this](storage::Status status) {
                                         if (status != storage::Status::OK) {
                                           errored_ = true;
                                           on_error_(ErrorType::PERMANENT);
                                           return;
                                         }

                                         // This object can be deleted in the
                                         // on_done_() callback, don't do
                                         // anything after the call.
                                         on_done_();
                                       }));
            }));
      }));
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
      on_error_(ErrorType::TEMPORARY);
      return;
    }

    auth_token_ = std::move(auth_token);
    on_refreshed();
  }));
}

}  // namespace cloud_sync
