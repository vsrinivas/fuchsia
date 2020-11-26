// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/transaction.h"

namespace blobfs {

void BlobTransaction::Commit(fs::Journal& journal, fit::promise<void, zx_status_t> data,
                             fit::callback<void()> callback) {
  zx_status_t status = journal.CommitTransaction({
      .metadata_operations = operations_.TakeOperations(),
      .data_promise = std::move(data),
      .trim = std::move(trim_),
      // We reserve extents (by capturing in a lambda) until after the trim has completed.
      .commit_callback = [reserved_extents = std::move(reserved_extents_)] {},
      .complete_callback = std::move(callback),
  });
  // We can't do much except log the error.
  if (status != ZX_OK) {
    FS_TRACE_ERROR("BlobTransaction::Commit failed: %s\n", zx_status_get_string(status));
  }
}

}  // namespace blobfs
