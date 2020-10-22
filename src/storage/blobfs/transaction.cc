// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/transaction.h"

namespace blobfs {

void BlobTransaction::Commit(fs::Journal& journal, fit::promise<void, zx_status_t> data,
                             fit::callback<void()> callback) {
  fit::pending_task task;
  auto do_callback = [callback = std::move(callback)]() mutable {
    if (callback)
      callback();
  };
  // The code is structured this way to minimise boxing of promises and is the reason why we only
  // support the combinations we need to.
  if (data) {
    ZX_ASSERT(trim_.empty() && reserved_extents_.empty());
    task = std::move(data)
               .and_then(journal.WriteMetadata(operations_.TakeOperations()))
               .and_then(std::move(do_callback));
  } else if (!trim_.empty() || !reserved_extents_.empty()) {
    // We reserve extents (by capturing in a lambda) until after the trim has completed.
    task = journal.WriteMetadata(operations_.TakeOperations())
               .and_then(std::move(do_callback))
               .and_then(journal.TrimData(std::move(trim_)))
               .and_then([reserved_extents = std::move(reserved_extents_)] {});
  } else {
    task = journal.WriteMetadata(operations_.TakeOperations()).and_then(std::move(do_callback));
  }
  journal.schedule_task(std::move(task));
}

}  // namespace blobfs
