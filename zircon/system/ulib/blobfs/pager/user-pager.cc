// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "user-pager.h"

#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/fzl/vmo-mapper.h>
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

  // Set up the decompress buffer.
  status = zx::vmo::create(kDecompressionBufferSize, 0, &decompression_buffer_);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Cannot create decompress buffer: %s\n", zx_status_get_string(status));
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

UserPager::ReadRange UserPager::GetBlockAlignedReadRange(UserPagerInfo* info, uint64_t offset,
                                                         uint64_t length) {
  ZX_DEBUG_ASSERT(offset < info->data_length_bytes);
  // Clamp the range to the size of the blob.
  length = fbl::min(length, info->data_length_bytes - offset);

  // Align to the block size for verification. (In practice this means alignment to 8k).
  zx_status_t status = info->verifier->Align(&offset, &length);
  // This only happens if the info->verifier thinks that [offset,length) is out of range, which
  // will only happen if |verifier| was initialized with a different length than the rest of |info|
  // (which is a programming error).
  ZX_DEBUG_ASSERT(status == ZX_OK);

  ZX_DEBUG_ASSERT(offset % kBlobfsBlockSize == 0);
  ZX_DEBUG_ASSERT(length % kBlobfsBlockSize == 0 || offset + length == info->data_length_bytes);

  return {.offset = offset, .length = length};
}

UserPager::ReadRange UserPager::GetBlockAlignedExtendedRange(UserPagerInfo* info, uint64_t offset,
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
  return GetBlockAlignedReadRange(info, read_ahead_offset, read_ahead_length);
}

zx_status_t UserPager::TransferPagesToVmo(uint64_t offset, uint64_t length, const zx::vmo& vmo,
                                          UserPagerInfo* info) {
  ZX_DEBUG_ASSERT(info);

  size_t end;
  if (add_overflow(offset, length, &end)) {
    FS_TRACE_ERROR("blobfs: Transfer range would overflow (off=%lu, len=%lu)\n", offset, length);
    return ZX_ERR_OUT_OF_RANGE;
  }

  return info->decompressor ? TransferCompressedPagesToVmo(offset, length, vmo, info)
                            : TransferUncompressedPagesToVmo(offset, length, vmo, info);
}

zx_status_t UserPager::TransferUncompressedPagesToVmo(uint64_t requested_offset,
                                                      uint64_t requested_length, const zx::vmo& vmo,
                                                      UserPagerInfo* info) {
  ZX_DEBUG_ASSERT(!info->decompressor);

  const auto [offset, length] =
      GetBlockAlignedExtendedRange(info, requested_offset, requested_length);

  TRACE_DURATION("blobfs", "UserPager::TransferUncompressedPagesToVmo", "offset", offset, "length",
                 length);

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
  const uint64_t rounded_length = fbl::round_up<uint64_t, uint64_t>(length, PAGE_SIZE);
  status = VerifyTransferVmo(offset, length, rounded_length, transfer_buffer_, info);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to verify transfer vmo: %s\n", zx_status_get_string(status));
    return status;
  }

  ZX_DEBUG_ASSERT(offset % PAGE_SIZE == 0);
  // Move the pages from the transfer buffer to the destination VMO.
  status = pager_.supply_pages(vmo, offset, rounded_length, transfer_buffer_, 0);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to supply pages to paged VMO: %s\n",
                   zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

zx_status_t UserPager::TransferCompressedPagesToVmo(uint64_t requested_offset,
                                                    uint64_t requested_length, const zx::vmo& vmo,
                                                    UserPagerInfo* info) {
  ZX_DEBUG_ASSERT(info->decompressor);

  const auto [offset, length] = GetBlockAlignedReadRange(info, requested_offset, requested_length);

  zx::status<CompressionMapping> mapping_status =
      info->decompressor->MappingForDecompressedRange(offset, length);
  if (!mapping_status.is_ok()) {
    FS_TRACE_ERROR("blobfs: Failed to find range for [%lu, %lu): %s\n", offset, offset + length,
                   mapping_status.status_string());
    return mapping_status.status_value();
  }
  CompressionMapping mapping = mapping_status.value();

  TRACE_DURATION("blobfs", "UserPager::TransferCompressedPagesToVmo", "offset",
                 mapping.decompressed_offset, "length", mapping.decompressed_length);

  auto decommit = fbl::MakeAutoCall([this, length = mapping.decompressed_length]() {
    // Decommit pages in the transfer buffer that might have been populated. All blobs share the
    // same transfer buffer - this prevents data leaks between different blobs.
    transfer_buffer_.op_range(ZX_VMO_OP_DECOMMIT, 0, fbl::round_up(length, kBlobfsBlockSize),
                              nullptr, 0);
  });

  // The compressed frame may not fall at a block aligned address, but we read in block aligned
  // chunks. This offset will be applied to the buffer we pass to decompression.
  // TODO(jfsulliv): Caching blocks which span frames may be useful for performance.
  size_t offset_of_compressed_data = mapping.compressed_offset % kBlobfsBlockSize;

  // Read from storage into the transfer buffer.
  size_t read_offset = fbl::round_down(mapping.compressed_offset, kBlobfsBlockSize);
  size_t read_len = (mapping.compressed_length + offset_of_compressed_data);
  zx_status_t status = PopulateTransferVmo(read_offset, read_len, info);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to populate transfer vmo: %s\n", zx_status_get_string(status));
    return status;
  }

  // Map the transfer VMO in order to pass the decompressor a pointer to the data.
  fzl::VmoMapper compressed_mapper;
  status = compressed_mapper.Map(transfer_buffer_, 0, read_len, ZX_VM_PERM_READ);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to map transfer buffer: %s\n", zx_status_get_string(status));
    return status;
  }
  auto unmap_compression = fbl::MakeAutoCall([&]() { compressed_mapper.Unmap(); });

  // Map the decompression VMO.
  fzl::VmoMapper decompressed_mapper;
  if ((status = decompressed_mapper.Map(decompression_buffer_, 0, mapping.decompressed_length,
                                        ZX_VM_PERM_READ | ZX_VM_PERM_WRITE)) != ZX_OK) {
    FS_TRACE_ERROR("Failed to map decompress buffer: %s\n", zx_status_get_string(status));
    return status;
  }
  auto unmap_decompression = fbl::MakeAutoCall([&]() { decompressed_mapper.Unmap(); });

  size_t decompressed_size = mapping.decompressed_length;
  uint8_t* src = static_cast<uint8_t*>(compressed_mapper.start()) + offset_of_compressed_data;
  status =
      info->decompressor->DecompressRange(decompressed_mapper.start(), &decompressed_size, src,
                                          mapping.compressed_length, mapping.decompressed_offset);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to decompress: %s\n", zx_status_get_string(status));
    return status;
  }

  // Verify the decompressed pages.
  const uint64_t rounded_length =
      fbl::round_up<uint64_t, uint64_t>(mapping.decompressed_length, PAGE_SIZE);
  status = info->verifier->VerifyPartial(decompressed_mapper.start(), mapping.decompressed_length,
                                         mapping.decompressed_offset, rounded_length);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to verify transfer vmo: %s\n", zx_status_get_string(status));
    return status;
  }

  decompressed_mapper.Unmap();

  // Move the pages from the decompression buffer to the destination VMO.
  status = pager_.supply_pages(vmo, mapping.decompressed_offset, rounded_length,
                               decompression_buffer_, 0);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to supply pages to paged VMO: %s\n",
                   zx_status_get_string(status));
    return status;
  }
  return ZX_OK;
}
}  // namespace blobfs
