// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zstd-seekable-blob.h"

#include <lib/fzl/owned-vmo-mapper.h>
#include <stdint.h>
#include <string.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <memory>
#include <vector>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fs/trace.h>
#include <zstd/zstd_seekable.h>

#include "zstd-seekable.h"

namespace blobfs {

namespace {

int ComputeOffsetAndNumBytesForRead(ZSTDSeekableFile* file, size_t num_bytes,
                                    uint32_t* out_data_block_offset, uint32_t* out_num_blocks,
                                    uint64_t* out_data_byte_offset) {
  // |file->byte_offset| does not account for ZSTD seekable header.
  uint64_t data_byte_offset;
  if (add_overflow(kZSTDSeekableHeaderSize, file->byte_offset, &data_byte_offset)) {
    FS_TRACE_ERROR("[blobfs][zstd-seekable] ZSTD header + file offset overflow: file_offset=%llu\n",
                   file->byte_offset);
    file->status = ZX_ERR_OUT_OF_RANGE;
    return -1;
  }

  // Safely convert units: Bytes to blocks.
  uint64_t data_block_start64 = data_byte_offset / kBlobfsBlockSize;
  if (data_block_start64 > std::numeric_limits<uint32_t>::max()) {
    FS_TRACE_ERROR("[blobfs][zstd-seekable] Oversized data block start: %lu / %u = %lu > %u\n",
                   data_byte_offset, kBlobfsBlockSize, data_block_start64,
                   std::numeric_limits<uint32_t>::max());
    file->status = ZX_ERR_OUT_OF_RANGE;
    return -1;
  }
  uint32_t data_block_start = static_cast<uint32_t>(data_block_start64);

  // Compute raw offset to end before determining which blocks must be read.
  uint64_t data_byte_end;
  if (add_overflow(data_byte_offset, num_bytes, &data_byte_end)) {
    FS_TRACE_ERROR(
        "[blobfs][zstd-seekable] Oversized data block end: data_byte_offset=%lu, num_bytes=%lu\n",
        data_byte_offset, num_bytes);
    file->status = ZX_ERR_OUT_OF_RANGE;
    return -1;
  }

  // Round up to nearest block from end, then subtract start to determine number of blocks.
  uint64_t data_block_end64 = fbl::round_up(data_byte_end, kBlobfsBlockSize) / kBlobfsBlockSize;
  uint64_t num_blocks64;
  if (sub_overflow(data_block_end64, data_block_start64, &num_blocks64)) {
    FS_TRACE_ERROR(
        "[blobfs][zstd-seekable] Block calculation error: (data_block_end=%lu - "
        "data_block_start=%lu) should be non-negative\n",
        data_block_end64, data_block_start64);
    file->status = ZX_ERR_INTERNAL;
    return -1;
  }
  if (num_blocks64 > std::numeric_limits<uint32_t>::max()) {
    FS_TRACE_ERROR("[blobfs][zstd-seekable] Oversized number of blocks: %lu > %u\n", num_blocks64,
                   std::numeric_limits<uint32_t>::max());
    file->status = ZX_ERR_OUT_OF_RANGE;
    return -1;
  }
  uint32_t num_blocks = static_cast<uint32_t>(num_blocks64);

  *out_data_block_offset = data_block_start;
  *out_num_blocks = num_blocks;
  *out_data_byte_offset = data_byte_offset;
  return 0;
}

}  // namespace

// ZSTD Seekable Format API function for `ZSTD_seekable_customFile`.
int ZSTDRead(void* void_ptr_zstd_seekable_file, void* buf, size_t num_bytes) {
  ZX_DEBUG_ASSERT(void_ptr_zstd_seekable_file);
  auto* file = static_cast<ZSTDSeekableFile*>(void_ptr_zstd_seekable_file);
  // Give up if any file operation has ever failed.
  if (file->status != ZX_OK) {
    return -1;
  }

  TRACE_DURATION("blobfs", "ZSTDRead", "byte_offset", file->byte_offset, "bytes", num_bytes);

  if (num_bytes == 0) {
    return 0;
  }

  uint32_t data_block_offset;
  uint32_t num_blocks;
  uint64_t data_byte_offset;
  int result = ComputeOffsetAndNumBytesForRead(file, num_bytes, &data_block_offset, &num_blocks,
                                               &data_byte_offset);
  if (result != 0) {
    // Note: |zx_status_t|, tracing/logging managed by |ComputeOffsetAndNumBytesForRead|.
    return result;
  }

  // Delegate block-level read to compressed block collection.
  zx_status_t status = file->blocks->Read(data_block_offset, num_blocks);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("[blobfs][zstd-seekable] Failed to read blocks: %s\n",
                   zx_status_get_string(status));
    file->status = status;
    return -1;
  }

  // Copy from transfer buffer to |buf|.
  {
    TRACE_DURATION("blobfs", "ZSTDRead::Copy", "byte_offset", file->byte_offset, "bytes",
                   num_bytes);
    uint32_t start = static_cast<uint32_t>(data_byte_offset % kBlobfsBlockSize);
    static_assert(sizeof(const void*) == sizeof(uint64_t));
    uint64_t start_ptr;
    uint64_t end_ptr;
    if (add_overflow(reinterpret_cast<uint64_t>(file->blob->decompressed_data_start()), start,
                     &start_ptr) ||
        add_overflow(start_ptr, num_bytes, &end_ptr)) {
      FS_TRACE_ERROR("[blobfs][zstd-seekable] VMO offset overflow: offset=%u length=%zu\n", start,
                     num_bytes);
      file->status = ZX_ERR_OUT_OF_RANGE;
      return -1;
    }
    memcpy(buf, file->blob->decompressed_data_start() + start, num_bytes);
  }

  // Advance byte offset in file.
  unsigned long long new_byte_offset;
  if (add_overflow(file->byte_offset, num_bytes, &new_byte_offset)) {
    FS_TRACE_ERROR("[blobfs][zstd-seekable] Byte offset overflow: file_offset=%llu increment=%lu\n",
                   file->byte_offset, num_bytes);
    file->status = ZX_ERR_OUT_OF_RANGE;
    return -1;
  }
  file->byte_offset = new_byte_offset;

  return 0;
}

// ZSTD Seekable Format API function for `ZSTD_seekable_customFile`.
int ZSTDSeek(void* void_ptr_zstd_seekable_file, long long byte_offset, int origin) {
  ZX_DEBUG_ASSERT(void_ptr_zstd_seekable_file);
  auto* file = static_cast<ZSTDSeekableFile*>(void_ptr_zstd_seekable_file);
  // Give up if any file operation has ever failed.
  if (file->status != ZX_OK) {
    return -1;
  }

  unsigned long long new_byte_offset = file->byte_offset;
  switch (origin) {
    // Absolute offset:
    // Set position in ZSTD archive to |byte_offset|.
    case SEEK_SET:
      if (byte_offset < 0) {
        FS_TRACE_ERROR("[blobfs][zstd-seekable] Seek absolute underflow: offset=%lld\n",
                       byte_offset);
        file->status = ZX_ERR_OUT_OF_RANGE;
        return -1;
      }
      new_byte_offset = byte_offset;
      break;
    // Relative-to-current offset:
    // Set position in ZSTD archive to |file->byte_offset + byte_offset|.
    case SEEK_CUR:
      if (byte_offset < 0 && file->byte_offset + byte_offset >= file->byte_offset) {
        FS_TRACE_ERROR(
            "[blobfs][zstd-seekable] Seek from current position underflow: current=%llu "
            "offset=%lld\n",
            file->byte_offset, byte_offset);
        file->status = ZX_ERR_OUT_OF_RANGE;
        return -1;
      }
      new_byte_offset = file->byte_offset + byte_offset;
      break;
    // Relative-to-end offset:
    // Set position in ZSTD archive to |file->num_bytes + byte_offset|.
    case SEEK_END:
      if (byte_offset < 0 && file->num_bytes + byte_offset >= file->num_bytes) {
        FS_TRACE_ERROR("[blobfs][zstd-seekable] Seek from end underflow: end=%llu offset=%lld\n",
                       file->num_bytes, byte_offset);
        file->status = ZX_ERR_OUT_OF_RANGE;
        return -1;
      }
      new_byte_offset = file->num_bytes + byte_offset;
      break;
    default:
      FS_TRACE_ERROR("[blobfs][zstd-seekable] Invalid seek origin enum value: %d\n", origin);
      return -1;
  }

  // New offset must not go passed end of file.
  if (new_byte_offset > file->num_bytes) {
    FS_TRACE_ERROR("[blobfs][zstd-seekable] Seek passed end of file: end=%llu offset=%lld\n",
                   file->num_bytes, new_byte_offset);
    file->status = ZX_ERR_OUT_OF_RANGE;
    return -1;
  }

  file->byte_offset = new_byte_offset;
  return 0;
}

zx_status_t ZSTDSeekableBlob::Create(
    fzl::OwnedVmoMapper* mapped_vmo,
    std::unique_ptr<ZSTDCompressedBlockCollection> compressed_block_collection,
    std::unique_ptr<ZSTDSeekableBlob>* out) {
  std::unique_ptr<ZSTDSeekableBlob> blob(
      new ZSTDSeekableBlob(mapped_vmo, std::move(compressed_block_collection)));
  zx_status_t status = blob->ReadHeader();
  if (status != ZX_OK) {
    return status;
  }

  *out = std::move(blob);
  return ZX_OK;
}

zx_status_t ZSTDSeekableBlob::Read(uint8_t* buf, uint64_t data_byte_offset, uint64_t num_bytes) {
  ZSTD_seekable* d_stream = ZSTD_seekable_create();
  if (d_stream == nullptr) {
    FS_TRACE_ERROR("[blobfs][zstd-seekable] Failed to create seekable dstream\n");
    return ZX_ERR_INTERNAL;
  }

  auto clean_up = fbl::MakeAutoCall([&]() { ZSTD_seekable_free(d_stream); });

  ZSTDSeekableFile zstd_seekable_file{
      .blob = this,
      .blocks = compressed_block_collection_.get(),
      .byte_offset = 0,
      .num_bytes = header_.archive_size,
      .status = ZX_OK,
  };
  size_t zstd_return = ZSTD_seekable_initAdvanced(d_stream, ZSTD_seekable_customFile{
                                                                .opaque = &zstd_seekable_file,
                                                                .read = ZSTDRead,
                                                                .seek = ZSTDSeek,
                                                            });
  if (ZSTD_isError(zstd_return)) {
    FS_TRACE_ERROR("[blobfs][zstd-seekable] Failed to initialize seekable dstream: %s\n",
                   ZSTD_getErrorName(zstd_return));
    return ZX_ERR_INTERNAL;
  }

  size_t decompressed = 0;
  do {
    zstd_return =
        ZSTD_seekable_decompress(d_stream, buf, num_bytes, data_byte_offset + decompressed);
    if (ZSTD_isError(zstd_return)) {
      FS_TRACE_ERROR("[blobfs][zstd-seekable] Failed to decompress: %s\n",
                     ZSTD_getErrorName(zstd_return));
      if (zstd_seekable_file.status != ZX_OK) {
        return zstd_seekable_file.status;
      }

      return ZX_ERR_IO_DATA_INTEGRITY;
    }

    // Non-error case: |ZSTD_seekable_decompress| returns number of bytes decompressed.
    decompressed += zstd_return;

    // From the ZSTD_seekable_decompress Documentation:
    //   The return value is the number of bytes decompressed, or an error code checkable with
    //   ZSTD_isError().
    // Assume that a return value of 0 indicates, not only that 0 bytes were decompressed, but also
    // that there are no more bytes to decompress.
  } while (zstd_return > 0 && decompressed < num_bytes);

  // TODO(markdittmer): Perform verification over block-aligned data that was read.

  return ZX_OK;
}

ZSTDSeekableBlob::ZSTDSeekableBlob(
    fzl::OwnedVmoMapper* mapped_vmo,
    std::unique_ptr<ZSTDCompressedBlockCollection> compressed_block_collection)
    : mapped_vmo_(mapped_vmo),
      compressed_block_collection_(std::move(compressed_block_collection)) {}

zx_status_t ZSTDSeekableBlob::ReadHeader() {
  // The header is an internal BlobFS data structure that fits into one block.
  static_assert(kZSTDSeekableHeaderSize <= kBlobfsBlockSize);
  const uint32_t read_num_blocks = 1;
  const size_t read_num_bytes = kBlobfsBlockSize;

  zx_status_t status = compressed_block_collection_->Read(0, read_num_blocks);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("[blobfs][zstd-seekable] Failed to read header block: %s\n",
                   zx_status_get_string(status));
    return status;
  }

  return ZSTDSeekableDecompressor::ReadHeader(mapped_vmo_->start(), read_num_bytes, &header_);
}

}  // namespace blobfs
