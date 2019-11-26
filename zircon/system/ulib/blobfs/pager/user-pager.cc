// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "user-pager.h"

#include <limits.h>
#include <zircon/status.h>

#include <memory>

#include <blobfs/format.h>
#include <fbl/auto_call.h>
#include <fs/trace.h>

namespace blobfs {

zx_status_t UserPager::InitPager() {
  TRACE_DURATION("blobfs", "UserPager::InitPager");

  // Make sure blocks are page-aligned.
  static_assert(kBlobfsBlockSize % PAGE_SIZE == 0);
  // Make sure the pager transfer buffer is block-aligned.
  static_assert(kTransferBufferSize % kBlobfsBlockSize == 0);

  // Set up the pager transfer buffer.
  zx_status_t status = zx::vmo::create(kTransferBufferSize, 0, &transfer_buffer_);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Cannot create transfer buffer: %s\n", zx_status_get_string(status));
    return status;
  }
  status = AttachTransferVmo(transfer_buffer_);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to attach transfer vmo: %s\n", zx_status_get_string(status));
    return status;
  }

  // Create the pager.
  status = zx::pager::create(0, &pager_);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Cannot initialize pager\n");
    return status;
  }

  // Start the pager thread.
  status = pager_loop_.StartThread("blobfs-pager-thread");
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Could not start pager thread\n");
    return status;
  }

  return ZX_OK;
}

zx_status_t UserPager::TransferPagesToVmo(uint32_t map_index, uint32_t start_block,
                                          uint32_t block_count, const zx::vmo& vmo) {
  TRACE_DURATION("blobfs", "UserPager::TransferPagesToVmo", "map_index", map_index, "start_block",
                 start_block, "block_count", block_count);

  auto decommit = fbl::MakeAutoCall([this, block_count]() {
    // Decommit pages in the transfer buffer that might have been populated. All blobs share the
    // same transfer buffer - this prevents data leaks between different blobs.
    transfer_buffer_.op_range(ZX_VMO_OP_DECOMMIT, 0, block_count * kBlobfsBlockSize, nullptr, 0);
  });

  // Read from storage into the transfer buffer.
  zx_status_t status = PopulateTransferVmo(map_index, start_block, block_count);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to populate transfer buffer: %s\n",
                   zx_status_get_string(status));
    return status;
  }

  // Move the pages from the transfer buffer to the destination VMO.
  status = pager_.supply_pages(vmo, start_block * kBlobfsBlockSize, block_count * kBlobfsBlockSize,
                               transfer_buffer_, 0);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to supply pages to paged VMO: %s\n",
                   zx_status_get_string(status));
    return status;
  }

  decommit.cancel();
  return ZX_OK;
}

}  // namespace blobfs
