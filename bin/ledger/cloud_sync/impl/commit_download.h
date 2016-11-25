// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_COMMIT_DOWNLOAD_H_
#define APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_COMMIT_DOWNLOAD_H_

#include "apps/ledger/src/cloud_provider/public/record.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "lib/ftl/functional/closure.h"

namespace cloud_sync {

// Adds a remote commit to storage.
//
// Sync does not explicitly download objects associated with commits. This class
// only makes a request to add the given remote commit to storage and handles
// the status once the operation completes. After CommitDownload makes the
// storage request and before the operation is confirmed, storage fetches the
// objects associated with the commit.
//
// The operation is not retryable, and errors reported through |error_callback|
// are not recoverable.
class CommitDownload {
 public:
  CommitDownload(storage::PageStorage* storage,
                 std::vector<cloud_provider::Record> records,
                 ftl::Closure on_done,
                 ftl::Closure error_callback);
  ~CommitDownload();

  // Can be called only once.
  void Start();

 private:
  storage::PageStorage* const storage_;
  std::vector<cloud_provider::Record> records_;
  ftl::Closure on_done_;
  ftl::Closure on_error_;
  bool started_ = false;

  FTL_DISALLOW_COPY_AND_ASSIGN(CommitDownload);
};

}  // namespace cloud_sync

#endif  // APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_COMMIT_DOWNLOAD_H_
