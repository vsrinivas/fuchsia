// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_BATCH_UPLOAD_H_
#define APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_BATCH_UPLOAD_H_

#include <functional>
#include <memory>
#include <queue>

#include "apps/ledger/src/auth_provider/auth_provider.h"
#include "apps/ledger/src/callback/cancellable.h"
#include "apps/ledger/src/cloud_provider/public/cloud_provider.h"
#include "apps/ledger/src/storage/public/commit.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"

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
  BatchUpload(storage::PageStorage* storage,
              cloud_provider_firebase::CloudProvider* cloud_provider,
              auth_provider::AuthProvider* auth_provider,
              std::vector<std::unique_ptr<const storage::Commit>> commits,
              ftl::Closure on_done,
              ftl::Closure on_error,
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

  // Uploads the given object.
  void UploadObject(std::unique_ptr<const storage::Object> object);

  // Filters already synced commits.
  void FilterAndUploadCommits();

  // Uploads the commits.
  void UploadCommits();

  void RefreshAuthToken(ftl::Closure on_refreshed);

  storage::PageStorage* const storage_;
  cloud_provider_firebase::CloudProvider* const cloud_provider_;
  auth_provider::AuthProvider* const auth_provider_;
  std::vector<std::unique_ptr<const storage::Commit>> commits_;
  ftl::Closure on_done_;
  ftl::Closure on_error_;
  const unsigned int max_concurrent_uploads_;

  // Auth token to be used for uploading the objects and the commit. It is
  // refreshed each time Start() or Retry() is called.
  std::string auth_token_;

  // All remaining object ids to be uploaded along with this batch of commits.
  std::queue<storage::ObjectId> remaining_object_ids_;

  // Number of object uploads currently in progress.
  unsigned int current_uploads_ = 0u;

  bool started_ = false;
  bool errored_ = false;

  // Pending auth token requests to be cancelled when this class goes away.
  callback::CancellableContainer auth_token_requests_;

  FTL_DISALLOW_COPY_AND_ASSIGN(BatchUpload);
};

}  // namespace cloud_sync

#endif  // APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_BATCH_UPLOAD_H_
