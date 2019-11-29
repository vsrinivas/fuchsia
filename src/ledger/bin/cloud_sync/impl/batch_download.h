// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_BATCH_DOWNLOAD_H_
#define SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_BATCH_DOWNLOAD_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/fit/function.h>

#include "src/ledger/bin/encryption/public/encryption_service.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace cloud_sync {

// Adds a batch of remote commits to storage.
//
// Given a list of commit metadata, this class makes a request to add them to
// storage, and waits until storage confirms that the operation completed before
// calling |on_done|.
//
// The operation is not retryable, and errors reported through |on_error| are
// not recoverable.
class BatchDownload {
 public:
  BatchDownload(storage::PageStorage* storage, encryption::EncryptionService* encryption_service,
                std::vector<cloud_provider::Commit> commits,
                std::unique_ptr<cloud_provider::PositionToken> position_token, fit::closure on_done,
                fit::closure on_error);
  ~BatchDownload();

  // Can be called only once.
  void Start();

 private:
  void UpdateTimestampAndQuit();

  storage::PageStorage* const storage_;
  encryption::EncryptionService* const encryption_service_;
  std::vector<cloud_provider::Commit> remote_commits_;
  std::unique_ptr<cloud_provider::PositionToken> position_token_;
  fit::closure on_done_;
  fit::closure on_error_;
  bool started_ = false;

  // Must be the last member.
  fxl::WeakPtrFactory<BatchDownload> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BatchDownload);
};

}  // namespace cloud_sync

#endif  // SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_BATCH_DOWNLOAD_H_
