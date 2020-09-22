// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/transaction/writeback.h>

#include <utility>
#include <vector>

namespace fs {

zx_status_t FlushRequests(LegacyTransactionHandler* transaction_handler,
                          const fbl::Vector<storage::BufferedOperation>& operations) {
  if (operations.is_empty()) {
    return ZX_OK;
  }

  // Update all the outgoing transactions to be in disk blocks.
  std::vector<block_fifo_request_t> blk_reqs;
  blk_reqs.resize(operations.size());
  const uint32_t block_per_fs_block =
      transaction_handler->FsBlockSize() / transaction_handler->DeviceBlockSize();
  for (size_t i = 0; i < operations.size(); i++) {
    blk_reqs[i].vmoid = operations[i].vmoid;
    switch (operations[i].op.type) {
      case storage::OperationType::kTrim:
        blk_reqs[i].opcode = BLOCKIO_TRIM;
        break;
      case storage::OperationType::kWrite:
        blk_reqs[i].opcode = BLOCKIO_WRITE;
        break;
      default:
        ZX_DEBUG_ASSERT_MSG(false, "Unsupported operation");
    }
    blk_reqs[i].vmo_offset = operations[i].op.vmo_offset * block_per_fs_block;
    blk_reqs[i].dev_offset = operations[i].op.dev_offset * block_per_fs_block;
    uint64_t length = operations[i].op.length * block_per_fs_block;
    // TODO(fxbug.dev/32112): Requests this long, although unlikely, should be
    // handled more gracefully.
    ZX_ASSERT_MSG(length < UINT32_MAX, "Request size too large");
    blk_reqs[i].length = static_cast<uint32_t>(length);
  }

  // Actually send the operations to the underlying block device.
  zx_status_t status = transaction_handler->Transaction(&blk_reqs[0], operations.size());

  return status;
}

}  // namespace fs
