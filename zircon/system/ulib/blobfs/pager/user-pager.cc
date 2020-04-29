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

UserPager::ReadRange UserPager::ExtendReadRange(UserPagerInfo* info, uint64_t offset,
                                                uint64_t length) {
  // TODO(rashaeqbal): Make the cluster size dynamic once we have prefetched read efficiency
  // metrics from the kernel - what percentage of prefetched pages are actually used.
  //
  // For now read read in at least 128KB (if the blob is larger than 128KB). 128KB is completely
  // arbitrary. Tune this for optimal performance (until we can support dynamic prefetch sizing).
  //
  // TODO(rashaeqbal): Consider extending the range backwards as well. Will need some way to track
  // populated ranges.
  constexpr uint64_t kReadAheadClusterSize = (128 * (1 << 10));

  size_t read_ahead_offset = offset;
  size_t read_ahead_length = fbl::max(kReadAheadClusterSize, length);
  read_ahead_length = fbl::min(read_ahead_length, info->data_length_bytes - read_ahead_offset);

  // Align to the block size for verification. (In practice this means alignment to 8k).
  zx_status_t status = info->verifier->Align(&read_ahead_offset, &read_ahead_length);
  // This only happens if the info->verifier thinks that [offset,length) is out of range, which
  // will only happen if |verifier| was initialized with a different length than the rest of |info|
  // (which is a programming error).
  ZX_DEBUG_ASSERT(status == ZX_OK);

  ZX_DEBUG_ASSERT(read_ahead_offset % kBlobfsBlockSize == 0);
  ZX_DEBUG_ASSERT(read_ahead_length % kBlobfsBlockSize == 0 ||
                  read_ahead_offset + read_ahead_length == info->data_length_bytes);

  return {.offset = read_ahead_offset, .length = read_ahead_length};
}

zx_status_t UserPager::TransferPagesToVmo(uint64_t requested_offset, uint64_t requested_length,
                                          const zx::vmo& vmo, UserPagerInfo* info) {
  ZX_DEBUG_ASSERT(info);

  size_t end;
  if (add_overflow(requested_offset, requested_length, &end)) {
    FS_TRACE_ERROR("blobfs: Transfer range would overflow (off=%lu, len=%lu)\n",
                   requested_offset, requested_length);
    return ZX_ERR_OUT_OF_RANGE;
  }
  const auto [offset, length] = ExtendReadRange(info, requested_offset, requested_length);

  TRACE_DURATION("blobfs", "UserPager::TransferPagesToVmo", "offset", offset, "length", length);

  auto decommit = fbl::MakeAutoCall([this, length = length]() {
    // Decommit pages in the transfer buffer that might have been populated. All blobs share the
    // same transfer buffer - this prevents data leaks between different blobs.
    transfer_buffer_.op_range(ZX_VMO_OP_DECOMMIT, 0, fbl::round_up(length, kBlobfsBlockSize),
                              nullptr, 0);
  });

  // Read from storage into the transfer buffer.
  zx_status_t status = PopulateTransferVmo(offset, length, info);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to populate transfer vmo: %s\n", zx_status_get_string(status));
    return status;
  }

  // Verify the pages read in.
  status = VerifyTransferVmo(offset, length, transfer_buffer_, info);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to verify transfer vmo: %s\n", zx_status_get_string(status));
    return status;
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
