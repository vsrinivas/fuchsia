// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_LIB_DISK_INSPECTOR_INCLUDE_DISK_INSPECTOR_LOADER_H_
#define SRC_STORAGE_LIB_DISK_INSPECTOR_INCLUDE_DISK_INSPECTOR_LOADER_H_

#include <storage/buffer/block_buffer.h>

#include "src/lib/storage/vfs/cpp/transaction/transaction_handler.h"

namespace disk_inspector {

// Wrapper arround fs::TransactionHandler to read/write on-disk structures from
// a block-device into a passed-in BlockBuffer.
class Loader {
 public:
  explicit Loader(fs::TransactionHandler* handler) : handler_(handler) {}

  // Wrapper to send a read operation into |buffer| at the specified locations
  // to the underlying TransactionHandler. Expects passed in |buffer| to be big
  // enough to write |length| blocks starting from |vmo_offset| from device.
  zx_status_t RunReadOperation(storage::BlockBuffer* buffer, uint64_t buffer_offset,
                               uint64_t dev_offset, uint64_t length) const;

  // Wrapper to send a write operation from |buffer| at the specified locations
  // to the underlying TransactionHandler. Expects passed in |buffer| to be big
  // enough to read |length| block starting from |vmo_offset| to device.
  zx_status_t RunWriteOperation(storage::BlockBuffer* buffer, uint64_t buffer_offset,
                                uint64_t dev_offset, uint64_t length) const;

 private:
  fs::TransactionHandler* handler_;
};
}  // namespace disk_inspector

#endif  // SRC_STORAGE_LIB_DISK_INSPECTOR_INCLUDE_DISK_INSPECTOR_LOADER_H_
