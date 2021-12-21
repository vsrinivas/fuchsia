// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_TRANSACTION_DEVICE_TRANSACTION_HANDLER_H_
#define SRC_LIB_STORAGE_VFS_CPP_TRANSACTION_DEVICE_TRANSACTION_HANDLER_H_

#include "src/lib/storage/block_client/cpp/block_device.h"
#include "src/lib/storage/vfs/cpp/transaction/transaction_handler.h"

namespace fs {

// Provides a reasonable implementation of RunRequests that issues requests to a BlockDevice.
class DeviceTransactionHandler : public TransactionHandler {
 public:
  zx_status_t RunRequests(const std::vector<storage::BufferedOperation>& operations) override;

  // Returns the backing block device that is associated with this TransactionHandler.
  virtual block_client::BlockDevice* GetDevice() = 0;

  zx_status_t Flush() override;
};

}  // namespace fs

#endif  // SRC_LIB_STORAGE_VFS_CPP_TRANSACTION_DEVICE_TRANSACTION_HANDLER_H_
