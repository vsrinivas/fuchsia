// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cloud_sync/impl/batch_upload.h"

#include <algorithm>
#include <set>
#include <utility>

#include <trace/event.h>

#include "lib/callback/scoped_callback.h"
#include "lib/callback/trace_callback.h"
#include "lib/callback/waiter.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/logging.h"

namespace cloud_sync {

BatchUpload::BatchUpload(
    storage::PageStorage* storage,
    encryption::EncryptionService* encryption_service,
    cloud_provider::PageCloudPtr* page_cloud,
    std::vector<std::unique_ptr<const storage::Commit>> commits,
    fxl::Closure on_done, std::function<void(ErrorType)> on_error,
    unsigned int max_concurrent_uploads)
    : storage_(storage),
      encryption_service_(encryption_service),
      page_cloud_(page_cloud),
      commits_(std::move(commits)),
      on_done_(std::move(on_done)),
      on_error_(std::move(on_error)),
      max_concurrent_uploads_(max_concurrent_uploads),
      weak_ptr_factory_(this) {
  TRACE_ASYNC_BEGIN("ledger", "batch_upload",
                    reinterpret_cast<uintptr_t>(this));
  FXL_DCHECK(storage_);
  FXL_DCHECK(page_cloud_);
}

BatchUpload::~BatchUpload() {
  TRACE_ASYNC_END("ledger", "batch_upload", reinterpret_cast<uintptr_t>(this));
}

void BatchUpload::Start() {
  FXL_DCHECK(!started_);
  FXL_DCHECK(!errored_);
  started_ = true;
  storage_->GetUnsyncedPieces(callback::MakeScoped(
      weak_ptr_factory_.GetWeakPtr(),
      [this](storage::Status status,
             std::vector<storage::ObjectIdentifier> object_identifiers) {
        if (status != storage::Status::OK) {
          errored_ = true;
          on_error_(ErrorType::PERMANENT);
          return;
        }
        remaining_object_identifiers_ = std::move(object_identifiers);
        StartObjectUpload();
      }));
}

void BatchUpload::Retry() {
  FXL_DCHECK(started_);
  FXL_DCHECK(errored_);
  errored_ = false;
  error_type_ = ErrorType::TEMPORARY;
  StartObjectUpload();
}

void BatchUpload::StartObjectUpload() {
  FXL_DCHECK(current_uploads_ == 0u);
  // If there are no unsynced objects left, upload the commits.
  if (remaining_object_identifiers_.empty()) {
    FilterAndUploadCommits();
    return;
  }

  while (current_uploads_ < max_concurrent_uploads_ &&
         !remaining_object_identifiers_.empty()) {
    UploadNextObject();
  }
}

void BatchUpload::UploadNextObject() {
  FXL_DCHECK(!remaining_object_identifiers_.empty());
  FXL_DCHECK(current_uploads_ < max_concurrent_uploads_);
  current_uploads_++;
  current_objects_handled_++;
  auto object_identifier_to_send =
      std::move(remaining_object_identifiers_.back());
  // Pop the object from the queue - if the upload fails, we will re-enqueue it.
  remaining_object_identifiers_.pop_back();

  // TODO(qsr): Retrieving the object name should be done in parallel with
  // retrieving the object content.
  encryption_service_->GetObjectName(
      object_identifier_to_send,
      callback::MakeScoped(
          weak_ptr_factory_.GetWeakPtr(),
          fxl::MakeCopyable([this, object_identifier_to_send](
                                encryption::Status encryption_status,
                                std::string object_name) mutable {
            if (encryption_status != encryption::Status::OK) {
              EnqueueForRetryAndSignalError(
                  std::move(object_identifier_to_send));
              return;
            }

            GetObjectContentAndUpload(std::move(object_identifier_to_send),
                                      std::move(object_name));
          })));
}

void BatchUpload::GetObjectContentAndUpload(
    storage::ObjectIdentifier object_identifier, std::string object_name) {
  storage_->GetPiece(
      object_identifier,
      callback::MakeScoped(
          weak_ptr_factory_.GetWeakPtr(),
          [this, object_identifier, object_name = std::move(object_name)](
              storage::Status storage_status,
              std::unique_ptr<const storage::Object> object) mutable {
            FXL_DCHECK(storage_status == storage::Status::OK);
            UploadObject(std::move(object_identifier), std::move(object_name),
                         std::move(object));
          }));
}

void BatchUpload::UploadObject(storage::ObjectIdentifier object_identifier,
                               std::string object_name,
                               std::unique_ptr<const storage::Object> object) {
  fsl::SizedVmo data;
  auto status = object->GetVmo(&data);
  // TODO(ppi): LE-225 Handle disk IO errors.
  FXL_DCHECK(status == storage::Status::OK);

  encryption_service_->EncryptObject(
      std::move(object_identifier), std::move(data),
      callback::MakeScoped(weak_ptr_factory_.GetWeakPtr(),
                           [this, object_identifier = object->GetIdentifier(),
                            object_name = std::move(object_name)](
                               encryption::Status encryption_status,
                               std::string encrypted_data) mutable {
                             if (encryption_status != encryption::Status::OK) {
                               EnqueueForRetryAndSignalError(
                                   std::move(object_identifier));
                               return;
                             }

                             UploadEncryptedObject(std::move(object_identifier),
                                                   std::move(object_name),
                                                   std::move(encrypted_data));
                           }));
}

void BatchUpload::UploadEncryptedObject(
    storage::ObjectIdentifier object_identifier, std::string object_name,
    std::string content) {
  fsl::SizedVmo data;
  if (!fsl::VmoFromString(content, &data)) {
    EnqueueForRetryAndSignalError(std::move(object_identifier));
    return;
  }

  (*page_cloud_)
      ->AddObject(
          convert::ToArray(object_name), std::move(data).ToTransport(),
          callback::MakeScoped(
              weak_ptr_factory_.GetWeakPtr(),
              [this, object_identifier = std::move(object_identifier)](
                  cloud_provider::Status status) mutable {
                FXL_DCHECK(current_uploads_ > 0);
                current_uploads_--;

                if (status != cloud_provider::Status::OK) {
                  EnqueueForRetryAndSignalError(std::move(object_identifier));
                  return;
                }

                // Uploading the object succeeded.
                storage_->MarkPieceSynced(
                    std::move(object_identifier),
                    callback::MakeScoped(
                        weak_ptr_factory_.GetWeakPtr(),
                        [this](storage::Status status) {
                          FXL_DCHECK(current_objects_handled_ > 0);
                          current_objects_handled_--;

                          if (status != storage::Status::OK) {
                            errored_ = true;
                            error_type_ = ErrorType::PERMANENT;
                          }

                          // Notify the user about the error once all pending
                          // operations of the recent retry complete.
                          if (errored_ && current_objects_handled_ == 0u) {
                            on_error_(error_type_);
                            return;
                          }

                          if (current_objects_handled_ == 0 &&
                              remaining_object_identifiers_.empty()) {
                            // All the referenced objects are uploaded and
                            // marked as synced, upload the commits.
                            FilterAndUploadCommits();
                            return;
                          }

                          if (!errored_ &&
                              !remaining_object_identifiers_.empty()) {
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
        std::set<storage::CommitId> commit_ids;
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
  auto waiter = fxl::MakeRefCounted<
      callback::Waiter<encryption::Status, cloud_provider::Commit>>(
      encryption::Status::OK);
  for (auto& storage_commit : commits_) {
    storage::CommitId id = storage_commit->GetId();
    encryption_service_->EncryptCommit(
        storage_commit->GetStorageBytes().ToString(),
        [id, callback = waiter->NewCallback()](
            encryption::Status status,
            std::string encrypted_storage_bytes) mutable {
          cloud_provider::Commit commit;
          commit.id = convert::ToArray(id);
          commit.data = convert::ToArray(encrypted_storage_bytes);
          callback(status, std::move(commit));
        });
    ids.push_back(std::move(id));
  }
  waiter->Finalize(callback::MakeScoped(
      weak_ptr_factory_.GetWeakPtr(),
      [this, ids = std::move(ids)](
          encryption::Status status,
          std::vector<cloud_provider::Commit> commits) mutable {
        if (status != encryption::Status::OK) {
          errored_ = true;
          on_error_(ErrorType::PERMANENT);
          return;
        }
        fidl::VectorPtr<cloud_provider::Commit> commit_array;
        for (auto& commit : commits) {
          commit_array.push_back(std::move(commit));
        }
        (*page_cloud_)
            ->AddCommits(
                std::move(commit_array),
                callback::MakeScoped(
                    weak_ptr_factory_.GetWeakPtr(),
                    [this, commit_ids =
                               std::move(ids)](cloud_provider::Status status) {
                      // UploadCommit() is called as a last step of a
                      // so-far-successful upload attempt, so we couldn't have
                      // failed before.
                      FXL_DCHECK(!errored_);
                      if (status != cloud_provider::Status::OK) {
                        errored_ = true;
                        on_error_(ErrorType::TEMPORARY);
                        return;
                      }
                      auto waiter = fxl::MakeRefCounted<
                          callback::StatusWaiter<storage::Status>>(
                          storage::Status::OK);

                      for (auto& id : commit_ids) {
                        storage_->MarkCommitSynced(id, waiter->NewCallback());
                      }
                      waiter->Finalize(callback::MakeScoped(
                          weak_ptr_factory_.GetWeakPtr(),
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

void BatchUpload::EnqueueForRetryAndSignalError(
    storage::ObjectIdentifier object_identifier) {
  FXL_DCHECK(current_objects_handled_ > 0);
  current_objects_handled_--;

  errored_ = true;
  // Re-enqueue the object for another upload attempt.
  remaining_object_identifiers_.push_back(std::move(object_identifier));

  if (current_objects_handled_ == 0u) {
    on_error_(error_type_);
  }
}

}  // namespace cloud_sync
