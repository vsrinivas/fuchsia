// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_PAGE_SYNC_DELEGATE_H_
#define PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_PAGE_SYNC_DELEGATE_H_

#include <functional>

#include <zx/socket.h>

#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/storage/public/types.h"

namespace storage {

// Delegate interface for PageStorage responsible for retrieving on-demand
// storage objects from the cloud.
class PageSyncDelegate {
 public:
  PageSyncDelegate() {}
  virtual ~PageSyncDelegate() {}

  // Retrieves the object of the given id from the cloud. The size of the object
  // is passed to the callback along with the socket handle, so that the
  // client can verify that all data was streamed when draining the socket.
  virtual void GetObject(
      ObjectDigestView object_digest,
      std::function<void(Status status, uint64_t size, zx::socket data)>
          callback) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageSyncDelegate);
};

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_PAGE_SYNC_DELEGATE_H_
