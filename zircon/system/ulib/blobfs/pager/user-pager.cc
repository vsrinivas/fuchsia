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

zx_status_t UserPager::TransferPagesToVmo(uint32_t map_index, uint64_t offset, uint64_t length,
                                          const zx::vmo& vmo, VerifierInfo* verifier_info) {
  TRACE_DURATION("blobfs", "UserPager::TransferPagesToVmo", "map_index", map_index, "offset",
                 offset, "length", length);

  auto decommit = fbl::MakeAutoCall([this, length]() {
    // Decommit pages in the transfer buffer that might have been populated. All blobs share the
    // same transfer buffer - this prevents data leaks between different blobs.
    transfer_buffer_.op_range(ZX_VMO_OP_DECOMMIT, 0, fbl::round_up(length, kBlobfsBlockSize),
                              nullptr, 0);
  });

  zx_status_t status;
  if (verifier_info) {
    // Align the range to include pages needed for verification.
    status = AlignForVerification(verifier_info, &offset, &length);
    if (status != ZX_OK) {
      return status;
    }
  }

  // Read from storage into the transfer buffer.
  status = PopulateTransferVmo(map_index, offset, length);
  if (status != ZX_OK) {
    return status;
  }

  // Verify the pages read in if a verifier is provided.
  if (verifier_info) {
    status = VerifyTransferVmo(verifier_info, transfer_buffer_, offset, length);
    if (status != ZX_OK) {
      return status;
    }
  }

  ZX_DEBUG_ASSERT(offset % PAGE_SIZE == 0);
  // Move the pages from the transfer buffer to the destination VMO.
  status = pager_.supply_pages(vmo, offset, fbl::round_up<uint64_t, uint64_t>(length, PAGE_SIZE),
                               transfer_buffer_, 0);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to supply pages to paged VMO: %s\n",
                   zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

}  // namespace blobfs
