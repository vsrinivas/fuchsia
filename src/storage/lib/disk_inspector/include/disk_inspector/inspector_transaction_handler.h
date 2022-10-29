// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_LIB_DISK_INSPECTOR_INCLUDE_DISK_INSPECTOR_INSPECTOR_TRANSACTION_HANDLER_H_
#define SRC_STORAGE_LIB_DISK_INSPECTOR_INCLUDE_DISK_INSPECTOR_INSPECTOR_TRANSACTION_HANDLER_H_

#include <lib/fpromise/result.h>
#include <zircon/types.h>

#include <memory>

#include <storage/buffer/block_buffer.h>
#include <storage/buffer/vmoid_registry.h>
#include <storage/operation/operation.h>

#include "src/lib/storage/block_client/cpp/block_device.h"
#include "src/lib/storage/vfs/cpp/transaction/device_transaction_handler.h"

namespace disk_inspector {

// Vmo-based implementation of DeviceTransactionHandler for use with disk-inspect application.
class InspectorTransactionHandler : public fs::DeviceTransactionHandler,
                                    public storage::VmoidRegistry {
 public:
  static zx_status_t Create(std::unique_ptr<block_client::BlockDevice> device, uint32_t block_size,
                            std::unique_ptr<InspectorTransactionHandler>* out);

  InspectorTransactionHandler(const InspectorTransactionHandler&) = delete;
  InspectorTransactionHandler(InspectorTransactionHandler&&) = delete;
  InspectorTransactionHandler& operator=(const InspectorTransactionHandler&) = delete;
  InspectorTransactionHandler& operator=(InspectorTransactionHandler&&) = delete;
  ~InspectorTransactionHandler() override = default;

  // fs::DeviceTransactionHandler interface:
  uint64_t BlockNumberToDevice(uint64_t block_num) const final;
  block_client::BlockDevice* GetDevice() final { return device_.get(); }

  // storage::VmoidRegistry interface:
  zx_status_t BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* out) final;
  zx_status_t BlockDetachVmo(storage::Vmoid vmoid) final;

 private:
  explicit InspectorTransactionHandler(std::unique_ptr<block_client::BlockDevice> device,
                                       fuchsia_hardware_block::wire::BlockInfo info,
                                       uint32_t block_size)
      : device_(std::move(device)), info_(info), block_size_(block_size) {}

  uint32_t FsBlockSize() const { return block_size_; }
  uint32_t DeviceBlockSize() const { return info_.block_size; }

  std::unique_ptr<block_client::BlockDevice> device_;
  fuchsia_hardware_block::wire::BlockInfo info_ = {};
  uint32_t block_size_;
};

}  // namespace disk_inspector

#endif  // SRC_STORAGE_LIB_DISK_INSPECTOR_INCLUDE_DISK_INSPECTOR_INSPECTOR_TRANSACTION_HANDLER_H_
