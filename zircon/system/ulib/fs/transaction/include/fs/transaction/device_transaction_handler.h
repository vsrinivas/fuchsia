// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_TRANSACTION_DEVICE_TRANSACTION_HANDLER_H_
#define FS_TRANSACTION_DEVICE_TRANSACTION_HANDLER_H_

#include <block-client/cpp/block-device.h>
#include <fs/transaction/transaction_handler.h>

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

#endif  // FS_TRANSACTION_DEVICE_TRANSACTION_HANDLER_H_
