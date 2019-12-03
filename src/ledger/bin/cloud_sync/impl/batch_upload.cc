// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/cloud_sync/impl/batch_upload.h"

#include <lib/fit/function.h>

#include <algorithm>
#include <set>
#include <utility>

#include <trace/event.h>

#include "src/ledger/bin/cloud_sync/impl/entry_payload_encoding.h"
#include "src/ledger/bin/cloud_sync/impl/status.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/ledger/lib/coroutine/coroutine_manager.h"
#include "src/ledger/lib/coroutine/coroutine_waiter.h"
#include "src/ledger/lib/encoding/encoding.h"
#include "src/ledger/lib/vmo/strings.h"
#include "src/lib/callback/scoped_callback.h"
#include "src/lib/callback/trace_callback.h"
#include "src/lib/callback/waiter.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/memory/ref_ptr.h"

namespace cloud_sync {

BatchUpload::UploadStatus BatchUpload::EncryptionStatusToUploadStatus(encryption::Status status) {
  if (status == encryption::Status::OK) {
    return UploadStatus::OK;
  }
  if (encryption::IsPermanentError(status)) {
    return UploadStatus::PERMANENT_ERROR;
  }
  return UploadStatus::TEMPORARY_ERROR;
}

BatchUpload::UploadStatus BatchUpload::LedgerStatusToUploadStatus(ledger::Status status) {
  if (status == ledger::Status::OK) {
    return UploadStatus::OK;
  }
  return UploadStatus::PERMANENT_ERROR;
}

BatchUpload::UploadStatus BatchUpload::CloudStatusToUploadStatus(cloud_provider::Status status) {
  if (status == cloud_provider::Status::OK) {
    return UploadStatus::OK;
  }
  if (IsPermanentError(status)) {
    return UploadStatus::PERMANENT_ERROR;
  }
  return UploadStatus::TEMPORARY_ERROR;
}

BatchUpload::ErrorType BatchUpload::UploadStatusToErrorType(BatchUpload::UploadStatus status) {
  switch (status) {
    case UploadStatus::OK:
      FXL_DCHECK(false) << "Not an error";
      return ErrorType::PERMANENT;
    case UploadStatus::TEMPORARY_ERROR:
      return ErrorType::TEMPORARY;
    case UploadStatus::PERMANENT_ERROR:
      return ErrorType::PERMANENT;
  }
}

BatchUpload::BatchUpload(coroutine::CoroutineService* coroutine_service,
                         storage::PageStorage* storage,
                         encryption::EncryptionService* encryption_service,
                         cloud_provider::PageCloudPtr* page_cloud,
                         std::vector<std::unique_ptr<const storage::Commit>> commits,
                         fit::closure on_done, fit::function<void(ErrorType)> on_error,
                         unsigned int max_concurrent_uploads)
    : storage_(storage),
      encryption_service_(encryption_service),
      page_cloud_(page_cloud),
      commits_(std::move(commits)),
      on_done_(std::move(on_done)),
      on_error_(std::move(on_error)),
      coroutine_manager_(coroutine_service, max_concurrent_uploads),
      weak_ptr_factory_(this) {
  TRACE_ASYNC_BEGIN("ledger", "batch_upload", reinterpret_cast<uintptr_t>(this));
  FXL_DCHECK(storage_);
  FXL_DCHECK(page_cloud_);
}

BatchUpload::~BatchUpload() {
  TRACE_ASYNC_END("ledger", "batch_upload", reinterpret_cast<uintptr_t>(this));
}

void BatchUpload::Start() {
  FXL_DCHECK(!started_);
  FXL_DCHECK(status_ == UploadStatus::OK);
  started_ = true;
  storage_->GetUnsyncedPieces(callback::MakeScoped(
      weak_ptr_factory_.GetWeakPtr(),
      [this](ledger::Status status, std::vector<storage::ObjectIdentifier> object_identifiers) {
        if (status != ledger::Status::OK) {
          SetUploadStatus(UploadStatus::PERMANENT_ERROR);
          SignalError();
          return;
        }
        remaining_object_identifiers_ = std::move(object_identifiers);
        StartObjectUpload();
      }));
}

void BatchUpload::Retry() {
  FXL_DCHECK(started_);
  FXL_DCHECK(status_ == UploadStatus::TEMPORARY_ERROR);
  status_ = UploadStatus::OK;
  StartObjectUpload();
}

void BatchUpload::StartObjectUpload() {
  // Use a completion waiter: even after an error, we still want to wait until all uploads in
  // progress complete before calling the error callback. Errors are tracked through the |status_|
  // variable.
  auto waiter = fxl::MakeRefCounted<callback::CompletionWaiter>();

  std::vector<storage::ObjectIdentifier> remaining_object_identifiers =
      std::move(remaining_object_identifiers_);
  remaining_object_identifiers_.clear();
  for (auto& identifier : remaining_object_identifiers) {
    coroutine_manager_.StartCoroutine(
        waiter->NewCallback(),
        [this, identifier = std::move(identifier)](coroutine::CoroutineHandler* handler,
                                                   fit::closure callback) mutable {
          if (status_ != UploadStatus::OK) {
            EnqueueForRetry(std::move(identifier));
          } else {
            SynchronousUploadObject(handler, std::move(identifier));
          }
          callback();
        });
  }

  waiter->Finalize([this]() {
    if (status_ != UploadStatus::OK) {
      SignalError();
      return;
    }
    FilterAndUploadCommits();
  });
}

void BatchUpload::SynchronousUploadObject(coroutine::CoroutineHandler* handler,
                                          storage::ObjectIdentifier object_identifier) {
  FXL_DCHECK(status_ == UploadStatus::OK);

  // While this waiter is alive and not cancelled, this function's stack frame is alive.
  auto waiter = fxl::MakeRefCounted<callback::StatusWaiter<UploadStatus>>(UploadStatus::OK);
  std::string object_name;
  encryption_service_->GetObjectName(
      object_identifier, waiter->MakeScoped([&object_name, callback = waiter->NewCallback()](
                                                encryption::Status status, std::string result) {
        object_name = result;
        callback(EncryptionStatusToUploadStatus(status));
      }));

  bool not_found = false;
  std::string encrypted_data;
  storage_->GetPiece(
      object_identifier,
      waiter->MakeScoped(
          [this, callback = waiter->NewCallback(), object_identifier, &encrypted_data, &waiter,
           &not_found](ledger::Status status, std::unique_ptr<const storage::Piece> piece) mutable {
            if (status != ledger::Status::OK) {
              if (status == ledger::Status::INTERNAL_NOT_FOUND) {
                not_found = true;
              }
              callback(LedgerStatusToUploadStatus(status));
              return;
            }
            encryption_service_->EncryptObject(
                object_identifier, piece->GetData(),
                waiter->MakeScoped([callback = std::move(callback), &encrypted_data](
                                       encryption::Status status, std::string result) {
                  encrypted_data = result;
                  callback(EncryptionStatusToUploadStatus(status));
                }));
          }));

  UploadStatus status;
  if (coroutine::Wait(handler, waiter, &status) == coroutine::ContinuationStatus::INTERRUPTED) {
    return;
  }

  if (not_found) {
    // The object is not in storage anymore, it does not need to be uploaded.
    return;
  }

  if (status != UploadStatus::OK) {
    SetUploadStatus(status);
    EnqueueForRetry(std::move(object_identifier));
    return;
  }

  ledger::SizedVmo data;
  if (!ledger::VmoFromString(encrypted_data, &data)) {
    SetUploadStatus(UploadStatus::PERMANENT_ERROR);
    return;
  }

  cloud_provider::Status cloud_status;
  if (coroutine::SyncCall(
          handler,
          [this, &object_name, &data](fit::function<void(cloud_provider::Status)> callback) {
            (*page_cloud_)
                ->AddObject(convert::ToArray(object_name), std::move(data).ToTransport(), {},
                            std::move(callback));
          },
          &cloud_status) == coroutine::ContinuationStatus::INTERRUPTED) {
    return;
  }

  if (cloud_status != cloud_provider::Status::OK) {
    SetUploadStatus(CloudStatusToUploadStatus(cloud_status));
    EnqueueForRetry(std::move(object_identifier));
    return;
  }

  ledger::Status storage_status;
  if (coroutine::SyncCall(
          handler,
          [this, &object_identifier](fit::function<void(ledger::Status)> callback) mutable {
            storage_->MarkPieceSynced(std::move(object_identifier), std::move(callback));
          },
          &storage_status) == coroutine::ContinuationStatus::INTERRUPTED) {
    return;
  }
  if (storage_status != ledger::Status::OK) {
    SetUploadStatus(UploadStatus::PERMANENT_ERROR);
    return;
  }
}

void BatchUpload::FilterAndUploadCommits() {
  // Remove all commits that have been synced since this upload object was
  // created. This will happen if a merge is executed on multiple devices at the
  // same time.
  storage_->GetUnsyncedCommits(callback::MakeScoped(
      weak_ptr_factory_.GetWeakPtr(),
      [this](ledger::Status status, std::vector<std::unique_ptr<const storage::Commit>> commits) {
        std::set<storage::CommitId> commit_ids;
        std::transform(
            commits.begin(), commits.end(), std::inserter(commit_ids, commit_ids.begin()),
            [](const std::unique_ptr<const storage::Commit>& commit) { return commit->GetId(); });

        commits_.erase(
            std::remove_if(commits_.begin(), commits_.end(),
                           [&commit_ids](const std::unique_ptr<const storage::Commit>& commit) {
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

void BatchUpload::EncodeCommit(
    const storage::Commit& commit,
    fit::function<void(UploadStatus, cloud_provider::Commit)> commit_callback) {
  auto waiter = fxl::MakeRefCounted<callback::StatusWaiter<UploadStatus>>(UploadStatus::OK);

  auto remote_commit = std::make_unique<cloud_provider::Commit>();
  auto remote_commit_ptr = remote_commit.get();

  remote_commit_ptr->set_id(convert::ToArray(encryption_service_->EncodeCommitId(commit.GetId())));
  encryption_service_->EncryptCommit(
      commit.GetStorageBytes().ToString(),
      waiter->MakeScoped([callback = waiter->NewCallback(), remote_commit_ptr](
                             encryption::Status status, std::string encrypted_commit) {
        if (status == encryption::Status::OK) {
          remote_commit_ptr->set_data(convert::ToArray(encrypted_commit));
        }
        callback(EncryptionStatusToUploadStatus(status));
      }));

  // This callback needs an additional level of scoping because EncodeDiff accesses the storage.
  storage_->GetDiffForCloud(
      commit,
      callback::MakeScoped(
          weak_ptr_factory_.GetWeakPtr(),
          waiter->MakeScoped([this, waiter, callback = waiter->NewCallback(), remote_commit_ptr](
                                 storage::Status status, storage::CommitIdView base_commit,
                                 std::vector<storage::EntryChange> changes) mutable {
            if (status != storage::Status::OK) {
              callback(LedgerStatusToUploadStatus(status));
              return;
            }
            EncodeDiff(base_commit, std::move(changes),
                       waiter->MakeScoped([callback = std::move(callback), remote_commit_ptr](
                                              UploadStatus status, cloud_provider::Diff diff) {
                         if (status == UploadStatus::OK) {
                           remote_commit_ptr->set_diff(std::move(diff));
                         }
                         callback(status);
                       }));
          })));

  waiter->Finalize([remote_commit = std::move(remote_commit),
                    commit_callback = std::move(commit_callback)](UploadStatus status) mutable {
    commit_callback(status, std::move(*remote_commit));
  });
}

void BatchUpload::EncodeDiff(storage::CommitIdView commit_id,
                             std::vector<storage::EntryChange> entries,
                             fit::function<void(UploadStatus, cloud_provider::Diff)> callback) {
  // We sort entries by their entry id. This ensures that the ordering of entries only depends on
  // information we are willing to reveal to the cloud.
  std::sort(entries.begin(), entries.end(),
            [](const storage::EntryChange& lhs, const storage::EntryChange& rhs) {
              return lhs.entry.entry_id < rhs.entry.entry_id;
            });

  auto waiter = fxl::MakeRefCounted<callback::Waiter<UploadStatus, cloud_provider::DiffEntry>>(
      UploadStatus::OK);

  cloud_provider::Diff diff;
  if (commit_id == storage::kFirstPageCommitId) {
    diff.mutable_base_state()->set_empty_page({});
  } else {
    diff.mutable_base_state()->set_at_commit(
        convert::ToArray(encryption_service_->EncodeCommitId(commit_id.ToString())));
  }

  for (auto& entry : entries) {
    EncodeEntry(std::move(entry), waiter->NewCallback());
  }

  waiter->Finalize(
      [diff = std::move(diff), callback = std::move(callback)](
          UploadStatus status, std::vector<cloud_provider::DiffEntry> entries) mutable {
        if (status != UploadStatus::OK) {
          callback(status, {});
          return;
        }
        diff.set_changes(std::move(entries));
        callback(status, std::move(diff));
      });
}

void BatchUpload::EncodeEntry(
    storage::EntryChange change,
    fit::function<void(UploadStatus, cloud_provider::DiffEntry)> callback) {
  cloud_provider::DiffEntry remote_entry;
  remote_entry.set_entry_id(convert::ToArray(change.entry.entry_id));
  remote_entry.set_operation(change.deleted ? cloud_provider::Operation::DELETION
                                            : cloud_provider::Operation::INSERTION);
  std::string entry_payload =
      EncodeEntryPayload(change.entry, storage_->GetObjectIdentifierFactory());
  encryption_service_->EncryptEntryPayload(
      std::move(entry_payload),
      [remote_entry = std::move(remote_entry), callback = std::move(callback)](
          encryption::Status status, std::string encrypted_entry_payload) mutable {
        if (status != encryption::Status::OK) {
          callback(UploadStatus::PERMANENT_ERROR, {});
          return;
        }
        remote_entry.set_data(convert::ToArray(encrypted_entry_payload));
        callback(UploadStatus::OK, std::move(remote_entry));
      });
}

void BatchUpload::UploadCommits() {
  FXL_DCHECK(status_ == UploadStatus::OK);
  std::vector<storage::CommitId> ids;
  auto waiter =
      fxl::MakeRefCounted<callback::Waiter<UploadStatus, cloud_provider::Commit>>(UploadStatus::OK);

  for (auto& storage_commit : commits_) {
    EncodeCommit(*storage_commit, waiter->NewCallback());
    ids.push_back(storage_commit->GetId());
  }

  waiter->Finalize(callback::MakeScoped(
      weak_ptr_factory_.GetWeakPtr(),
      [this, ids = std::move(ids)](UploadStatus status,
                                   std::vector<cloud_provider::Commit> commits) mutable {
        if (status != UploadStatus::OK) {
          SetUploadStatus(status);
          SignalError();
          return;
        }
        cloud_provider::CommitPack commit_pack;
        cloud_provider::Commits commits_container{std::move(commits)};
        if (!ledger::EncodeToBuffer(&commits_container, &commit_pack.buffer)) {
          SetUploadStatus(UploadStatus::PERMANENT_ERROR);
          SignalError();
          return;
        }
        (*page_cloud_)
            ->AddCommits(std::move(commit_pack),
                         callback::MakeScoped(
                             weak_ptr_factory_.GetWeakPtr(),
                             [this, commit_ids = std::move(ids)](cloud_provider::Status status) {
                               // UploadCommit() is called as a last step of a
                               // so-far-successful upload attempt, so we couldn't have
                               // failed before.
                               FXL_DCHECK(status_ == UploadStatus::OK);
                               if (status != cloud_provider::Status::OK) {
                                 SetUploadStatus(CloudStatusToUploadStatus(status));
                                 SignalError();
                                 return;
                               }
                               auto waiter =
                                   fxl::MakeRefCounted<callback::StatusWaiter<ledger::Status>>(
                                       ledger::Status::OK);

                               for (auto& id : commit_ids) {
                                 storage_->MarkCommitSynced(id, waiter->NewCallback());
                               }
                               waiter->Finalize(callback::MakeScoped(
                                   weak_ptr_factory_.GetWeakPtr(), [this](ledger::Status status) {
                                     if (status != ledger::Status::OK) {
                                       SetUploadStatus(UploadStatus::PERMANENT_ERROR);
                                       SignalError();
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

void BatchUpload::EnqueueForRetry(storage::ObjectIdentifier object_identifier) {
  remaining_object_identifiers_.push_back(std::move(object_identifier));
}

void BatchUpload::SetUploadStatus(UploadStatus status) { status_ = std::max(status_, status); }

void BatchUpload::SignalError() { on_error_(UploadStatusToErrorType(status_)); }

}  // namespace cloud_sync
