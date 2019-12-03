// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_BATCH_UPLOAD_H_
#define SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_BATCH_UPLOAD_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/fit/function.h>

#include <functional>
#include <memory>
#include <vector>

#include "src/ledger/bin/encryption/public/encryption_service.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/ledger/lib/coroutine/coroutine_manager.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace cloud_sync {

// Uploads a batch of commits along with unsynced storage objects and marks
// the uploaded artifacts as synced.
//
// Contract: The class doesn't reason about objects referenced by each commit,
// and instead uploads each unsynced object present in storage at the moment of
// calling Start(). Unsynced objects are marked as synced as they are uploaded.
// The commits in the batch are uploaded in one network request once all objects
// are uploaded.
//
// Usage: call Start() to kick off the upload. |on_done| is called after the
// upload is successfully completed. |on_error| will be called at most once
// after each error. Each time after |on_error| is called the client can
// call Retry() once to retry the upload.
// TODO(ppi): rather than DCHECK on storage errors, take separate callbacks for
// network and disk errors and let PageSync decide on how to handle each.
//
// Lifetime: if BatchUpload is deleted between Start() and |on_done| being
// called, it has to be deleted along with |storage| and |cloud_provider|, which
// otherwise can retain callbacks for pending uploads. This isn't a problem as
// long as the lifetime of page storage and page sync is managed together.
class BatchUpload {
 public:
  // In case of error in BatchUpload, ErrorType defines whether the error that
  // occurred is temporary (from cloud or auth provider), or permanent (from
  // storage or from encryption).
  enum class ErrorType {
    PERMANENT,
    TEMPORARY,
  };

  BatchUpload(coroutine::CoroutineService* coroutine_service, storage::PageStorage* storage,
              encryption::EncryptionService* encryption_service,
              cloud_provider::PageCloudPtr* page_cloud,
              std::vector<std::unique_ptr<const storage::Commit>> commits, fit::closure on_done,
              fit::function<void(ErrorType)> on_error, unsigned int max_concurrent_uploads = 10);
  BatchUpload(const BatchUpload&) = delete;
  BatchUpload& operator=(const BatchUpload&) = delete;
  ~BatchUpload();

  // Starts a new upload attempt. Results are reported through |on_done|
  // and |on_error| passed in the constructor. Can be called only once.
  void Start();

  // Retries the attempt to upload the commit batch. Each time |on_error| is called with a temporary
  // error, the client can retry by calling this method.
  void Retry();

 private:
  // Status of an upload operation: successful, or the type of the error to return.
  // This is ordered from best status to worst status.
  enum class UploadStatus {
    OK,
    TEMPORARY_ERROR,
    PERMANENT_ERROR,
  };

  // Converts an encryption status to an upload status.
  static UploadStatus EncryptionStatusToUploadStatus(encryption::Status status);

  // Converts a ledger status to an upload status.
  static UploadStatus LedgerStatusToUploadStatus(ledger::Status status);

  // Converts a cloud provider status to an upload status.
  static UploadStatus CloudStatusToUploadStatus(cloud_provider::Status status);

  // Converts a non-OK upload status to its error type.
  static ErrorType UploadStatusToErrorType(UploadStatus status);

  // Sets the upload status to |status| if it is worse than the current status.
  void SetUploadStatus(UploadStatus status);

  // Calls |on_error_| with the current error status.
  void SignalError();

  void StartObjectUpload();

  // Reads, encrypts and uploads one object. Errors are signaled in |status_|. The caller is
  // responsible for calling the |on_error_| callback when appropriate.
  void SynchronousUploadObject(coroutine::CoroutineHandler* handler,
                               storage::ObjectIdentifier identifier);

  // Filters already synced commits.
  void FilterAndUploadCommits();

  // Uploads the commits.
  void UploadCommits();

  // Re-enqueue the object for another upload attempt.
  void EnqueueForRetry(storage::ObjectIdentifier object_identifier);

  // Encodes a commit for sending to the cloud.
  void EncodeCommit(const storage::Commit& commit,
                    fit::function<void(UploadStatus, cloud_provider::Commit)> callback);

  // Encodes a diff for sending to the cloud.
  void EncodeDiff(storage::CommitIdView commit_id, std::vector<storage::EntryChange> entries,
                  fit::function<void(UploadStatus, cloud_provider::Diff)> callback);

  // Encodes a diff entry for sending to the cloud.
  void EncodeEntry(storage::EntryChange entry,
                   fit::function<void(UploadStatus, cloud_provider::DiffEntry)> callback);

  storage::PageStorage* const storage_;
  encryption::EncryptionService* const encryption_service_;
  cloud_provider::PageCloudPtr* const page_cloud_;
  std::vector<std::unique_ptr<const storage::Commit>> commits_;
  fit::closure on_done_;
  fit::function<void(ErrorType)> on_error_;

  // All remaining object ids to be uploaded along with this batch of commits.
  std::vector<storage::ObjectIdentifier> remaining_object_identifiers_;

  bool started_ = false;

  // Stores the status of the upload. If multiple errors have been encountered, stores the worst
  // error (permanent if any permanent error has been encountered, temporary otherwise).
  //
  // Transitions: this always goes from best to worst (OK -> TEMPORARY_ERROR -> PERMANENT_ERROR),
  // except in |Retry| where the status can transition from |TEMPORARY_ERROR| to |OK|.
  UploadStatus status_ = UploadStatus::OK;

  coroutine::CoroutineManager coroutine_manager_;

  // Must be the last member.
  fxl::WeakPtrFactory<BatchUpload> weak_ptr_factory_;
};

}  // namespace cloud_sync

#endif  // SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_BATCH_UPLOAD_H_
