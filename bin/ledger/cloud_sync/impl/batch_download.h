// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_BATCH_DOWNLOAD_H_
#define PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_BATCH_DOWNLOAD_H_

#include "garnet/public/lib/fidl/cpp/bindings/array.h"
#include "lib/cloud_provider/fidl/cloud_provider.fidl.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "peridot/bin/ledger/cloud_provider/public/record.h"
#include "peridot/bin/ledger/encryption/public/encryption_service.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"

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
  BatchDownload(storage::PageStorage* storage,
                encryption::EncryptionService* encryption_service,
                fidl::Array<cloud_provider::CommitPtr> commits,
                fidl::Array<uint8_t> position_token,
                fxl::Closure on_done,
                fxl::Closure on_error);
  ~BatchDownload();

  // Can be called only once.
  void Start();

 private:
  void UpdateTimestampAndQuit();

  storage::PageStorage* const storage_;
  encryption::EncryptionService* const encryption_service_;
  fidl::Array<cloud_provider::CommitPtr> commits_;
  fidl::Array<uint8_t> position_token_;
  fxl::Closure on_done_;
  fxl::Closure on_error_;
  bool started_ = false;

  // Must be the last member.
  fxl::WeakPtrFactory<BatchDownload> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BatchDownload);
};

}  // namespace cloud_sync

#endif  // PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_BATCH_DOWNLOAD_H_
