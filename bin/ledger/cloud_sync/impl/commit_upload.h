// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_COMMIT_UPLOAD_H_
#define APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_COMMIT_UPLOAD_H_

#include <functional>
#include <memory>

#include "apps/ledger/src/cloud_provider/public/cloud_provider.h"
#include "apps/ledger/src/storage/public/commit.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "lib/ftl/macros.h"

namespace cloud_sync {

// Uploads a single commit along with the storage objects referenced by it
// through the cloud provider and marks the uploaded artifacts as synced.
//
// Contract: Unsynced objects referenced by the commit are marked as synced as
// they are uploaded. The commit itself is uploaded only once all objects are
// uploaded. The entire commit is marked as synced once all objects are uploaded
// and the commit itself is uploaded.
//
// Usage: call Start() to kick off the upload. |done_callback| is called after
// upload is successfully completed. |error_callback| will be called at most
// once after each Start() call when an error occurs. After |error_callback| is
// called the client can call Start() again to retry the upload.
//
// Lifetime: if CommitUpload is deleted between Start() and |done_callback|
// being called, it has to be deleted along with |storage| and |cloud_provider|,
// which otherwise can retain callbacks for pending uploads. This isn't a
// problem as long as the lifetime of page storage and page sync is managed
// together.
class CommitUpload {
 public:
  CommitUpload(storage::PageStorage* storage,
               cloud_provider::CloudProvider* cloud_provider,
               std::unique_ptr<const storage::Commit> commit,
               std::function<void()> done_callback,
               std::function<void()> error_callback);
  ~CommitUpload();

  // Starts a new upload attempt. Results are reported through |done_callback|
  // and |error_callback| passed in the constructor. After |error_callback| is
  // called the client can retry by calling Start() again.
  void Start();

 private:
  // Uploads the given object.
  void UploadObject(std::unique_ptr<const storage::Object> object);

  // Uploads the commit.
  void UploadCommit();

  storage::PageStorage* storage_;
  cloud_provider::CloudProvider* cloud_provider_;
  std::unique_ptr<const storage::Commit> commit_;
  std::function<void()> done_callback_;
  std::function<void()> error_callback_;
  // Incremented on every upload attempt / Start() call. Tracked to detect stale
  // callbacks executing for the previous upload attempts.
  int current_attempt_ = 0;
  // True iff the current upload attempt is active, ie. didn't error yet.
  // Tracked to guard against starting a new upload attempt before the previous
  // one fails and to avoid duplicate |error_callback| calls for a single upload
  // attempt. This is not reset after completing the upload, so that it's an
  // error to call .Start() on an upload that is complete.
  bool active_or_finished_ = false;
  // Count of the remaining objects to be uploaded in the current upload
  // attempt.
  int objects_to_upload_ = 0;

  FTL_DISALLOW_COPY_AND_ASSIGN(CommitUpload);
};

}  // namespace cloud_sync

#endif  // APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_COMMIT_UPLOAD_H_
