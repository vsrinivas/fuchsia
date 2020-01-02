// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/disk-inspector/inspector-transaction-handler.h"

#include <fcntl.h>
#include <lib/fdio/fdio.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <block-client/cpp/remote-block-device.h>
#include <fs/trace.h>
#include <safemath/checked_math.h>
#include <storage/buffer/vmo-buffer.h>

namespace disk_inspector {

zx_status_t InspectorTransactionHandler::Create(std::unique_ptr<block_client::BlockDevice> device,
                                                uint32_t block_size,
                                                std::unique_ptr<InspectorTransactionHandler>* out) {
  fuchsia_hardware_block_BlockInfo info;
  zx_status_t status = device->BlockGetInfo(&info);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Cannot get block device information: %d\n", status);
    return status;
  }
  if (info.block_size == 0 || block_size % info.block_size != 0) {
    FS_TRACE_ERROR("fs block size: %d not multiple of underlying block size: %d\n", block_size,
                   info.block_size);
    return ZX_ERR_NOT_SUPPORTED;
  }
  out->reset(new InspectorTransactionHandler(std::move(device), info, block_size));
  return ZX_OK;
}

uint64_t InspectorTransactionHandler::BlockNumberToDevice(uint64_t block_num) const {
  return block_num * FsBlockSize() / DeviceBlockSize();
}

zx_status_t InspectorTransactionHandler::RunOperation(const storage::Operation& operation,
                                                      storage::BlockBuffer* buffer) {
  if (operation.type != storage::OperationType::kWrite &&
      operation.type != storage::OperationType::kRead) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  block_fifo_request_t request;
  request.group = BlockGroupID();
  request.vmoid = buffer->vmoid();
  request.opcode = operation.type == storage::OperationType::kWrite ? BLOCKIO_WRITE : BLOCKIO_READ;
  request.vmo_offset = BlockNumberToDevice(operation.vmo_offset);
  request.dev_offset = BlockNumberToDevice(operation.dev_offset);
  uint64_t length = BlockNumberToDevice(operation.length);
  if (length > UINT32_MAX) {
    FS_TRACE_ERROR("Operation length larger than uint32_max.");
    return ZX_ERR_INVALID_ARGS;
  }
  request.length = safemath::checked_cast<uint32_t>(length);

  return device_->FifoTransaction(&request, 1);
}

zx_status_t InspectorTransactionHandler::AttachVmo(const zx::vmo& vmo, vmoid_t* out) {
  fuchsia_hardware_block_VmoID vmoid;
  zx_status_t status = device_->BlockAttachVmo(vmo, &vmoid);
  *out = vmoid.id;
  return status;
}

zx_status_t InspectorTransactionHandler::DetachVmo(vmoid_t vmoid) {
  block_fifo_request_t request = {};
  request.group = BlockGroupID();
  request.vmoid = vmoid;
  request.opcode = BLOCKIO_CLOSE_VMO;
  return device_->FifoTransaction(&request, 1);
}

}  // namespace disk_inspector
