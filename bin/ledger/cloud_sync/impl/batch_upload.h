// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_BATCH_UPLOAD_H_
#define PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_BATCH_UPLOAD_H_

#include <functional>
#include <memory>
#include <vector>

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/fit/function.h>

#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "peridot/bin/ledger/encryption/public/encryption_service.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/storage/public/commit.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"

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

  BatchUpload(storage::PageStorage* storage,
              encryption::EncryptionService* encryption_service,
              cloud_provider::PageCloudPtr* page_cloud,
              std::vector<std::unique_ptr<const storage::Commit>> commits,
              fit::closure on_done, fit::function<void(ErrorType)> on_error,
              unsigned int max_concurrent_uploads = 10);
  ~BatchUpload();

  // Starts a new upload attempt. Results are reported through |on_done|
  // and |on_error| passed in the constructor. Can be called only once.
  void Start();

  // Retries the attempt to upload the commit batch. Each time after |on_error|
  // is called, the client can retry by calling this method.
  void Retry();

 private:
  void StartObjectUpload();

  void UploadNextObject();

  // Retrieves the content of the given object and uploads it.
  void GetObjectContentAndUpload(storage::ObjectIdentifier object_identifier,
                                 std::string object_name);

  // Uploads the given object.
  void UploadObject(storage::ObjectIdentifier object_identifier,
                    std::string object_name,
                    std::unique_ptr<const storage::Object> object);

  // Uploads the given object.
  void UploadEncryptedObject(storage::ObjectIdentifier object_identifier,
                             std::string object_name, std::string content);

  // Filters already synced commits.
  void FilterAndUploadCommits();

  // Uploads the commits.
  void UploadCommits();

  // Notifies an error when trying to upload the given object.
  void EnqueueForRetryAndSignalError(
      storage::ObjectIdentifier object_identifier);

  storage::PageStorage* const storage_;
  encryption::EncryptionService* const encryption_service_;
  cloud_provider::PageCloudPtr* const page_cloud_;
  std::vector<std::unique_ptr<const storage::Commit>> commits_;
  fit::closure on_done_;
  fit::function<void(ErrorType)> on_error_;
  const unsigned int max_concurrent_uploads_;

  // All remaining object ids to be uploaded along with this batch of commits.
  std::vector<storage::ObjectIdentifier> remaining_object_identifiers_;

  // Number of object uploads currently in progress.
  unsigned int current_uploads_ = 0u;

  // Number of object being handled, including those being uploaded and those
  // whose metadata are being updated in storage.
  unsigned int current_objects_handled_ = 0u;

  bool started_ = false;
  bool errored_ = false;
  // If an error has occurred while handling the objects, |error_types_|
  // stores the type of error.
  ErrorType error_type_ = ErrorType::TEMPORARY;

  // Must be the last member.
  fxl::WeakPtrFactory<BatchUpload> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BatchUpload);
};

}  // namespace cloud_sync

#endif  // PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_BATCH_UPLOAD_H_
