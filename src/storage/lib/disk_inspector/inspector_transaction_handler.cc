// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "disk_inspector/inspector_transaction_handler.h"

#include <fcntl.h>
#include <lib/fdio/fdio.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <safemath/checked_math.h>
#include <storage/buffer/vmo_buffer.h>

namespace disk_inspector {

zx_status_t InspectorTransactionHandler::Create(std::unique_ptr<block_client::BlockDevice> device,
                                                uint32_t block_size,
                                                std::unique_ptr<InspectorTransactionHandler>* out) {
  fuchsia_hardware_block::wire::BlockInfo info;
  zx_status_t status = device->BlockGetInfo(&info);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot get block device information: " << status;
    return status;
  }
  if (info.block_size == 0 || block_size % info.block_size != 0) {
    FX_LOGS(INFO) << "fs block size: " << block_size
                  << " not multiple of underlying block size: " << info.block_size;
    return ZX_ERR_NOT_SUPPORTED;
  }
  out->reset(new InspectorTransactionHandler(std::move(device), info, block_size));
  return ZX_OK;
}

uint64_t InspectorTransactionHandler::BlockNumberToDevice(uint64_t block_num) const {
  return block_num * FsBlockSize() / DeviceBlockSize();
}

zx_status_t InspectorTransactionHandler::BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* out) {
  zx_status_t status = device_->BlockAttachVmo(vmo, out);
  return status;
}

zx_status_t InspectorTransactionHandler::BlockDetachVmo(storage::Vmoid vmoid) {
  return device_->BlockDetachVmo(std::move(vmoid));
}

}  // namespace disk_inspector
