// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chunked.h"

#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <fs/trace.h>
#include <lib/zx/status.h>
#include <src/lib/chunked-compression/chunked-archive.h>
#include <src/lib/chunked-compression/chunked-decompressor.h>
#include <src/lib/chunked-compression/status.h>
#include <src/lib/chunked-compression/streaming-chunked-compressor.h>

namespace blobfs {

namespace {

constexpr int kDefaultLevel = 3;

using chunked_compression::Status;
using chunked_compression::ToZxStatus;

}  // namespace

// ChunkedCompressor

ChunkedCompressor::ChunkedCompressor(chunked_compression::StreamingChunkedCompressor compressor,
                                     size_t input_len)
    : compressor_(std::move(compressor)), input_len_(input_len) {}

zx_status_t ChunkedCompressor::Create(size_t input_size, size_t* output_limit_out,
                                      std::unique_ptr<ChunkedCompressor>* out) {
  chunked_compression::CompressionParams params;
  params.compression_level = kDefaultLevel;
  params.chunk_size = chunked_compression::CompressionParams::ChunkSizeForInputSize(input_size);

  chunked_compression::StreamingChunkedCompressor compressor(params);

  *output_limit_out = compressor.ComputeOutputSizeLimit(input_size);
  *out =
      std::unique_ptr<ChunkedCompressor>(new ChunkedCompressor(std::move(compressor), input_size));

  return ZX_OK;
}

zx_status_t ChunkedCompressor::SetOutput(void* dst, size_t dst_len) {
  if (dst_len < compressor_.ComputeOutputSizeLimit(input_len_)) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  if (compressor_.Init(input_len_, dst, dst_len) != chunked_compression::kStatusOk) {
    FS_TRACE_ERROR("blobfs: Failed to initialize compressor\n");
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

size_t ChunkedCompressor::Size() const { return compressed_size_.value_or(0ul); }

zx_status_t ChunkedCompressor::Update(const void* input_data, size_t input_length) {
  TRACE_DURATION("blobfs", "ChunkedCompressor::Update", "input_length", input_length);
  if (compressor_.Update(input_data, input_length) != chunked_compression::kStatusOk) {
    FS_TRACE_ERROR("blobfs: Compression failed.\n");
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

zx_status_t ChunkedCompressor::End() {
  TRACE_DURATION("blobfs", "ChunkedCompressor::End");
  size_t sz;
  Status status = compressor_.Final(&sz);
  if (status != chunked_compression::kStatusOk) {
    FS_TRACE_ERROR("blobfs: Compression failed.\n");
    return ZX_ERR_INTERNAL;
  }
  compressed_size_ = sz;
  return ZX_OK;
}

// ChunkedDecompressor

zx_status_t ChunkedDecompressor::Decompress(void* uncompressed_buf, size_t* uncompressed_size,
                                            const void* compressed_buf,
                                            const size_t max_compressed_size) {
  TRACE_DURATION("blobfs", "ChunkedCompressor::Decompress", "compressed_size", max_compressed_size);
  chunked_compression::SeekTable seek_table;
  chunked_compression::HeaderReader reader;
  Status status =
      reader.Parse(compressed_buf, max_compressed_size, max_compressed_size, &seek_table);
  if (status != chunked_compression::kStatusOk) {
    FS_TRACE_ERROR("blobfs: Invalid archive header.\n");
    return ToZxStatus(status);
  }
  size_t decompression_buf_size = *uncompressed_size;
  if (decompressor_.Decompress(seek_table, compressed_buf, max_compressed_size, uncompressed_buf,
                               decompression_buf_size,
                               uncompressed_size) != chunked_compression::kStatusOk) {
    FS_TRACE_ERROR("blobfs: Failed to decompress archive.\n");
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  return ZX_OK;
}

// SeekableChunkedDecompressor

zx_status_t SeekableChunkedDecompressor::CreateDecompressor(
    const void* seek_table_buf, size_t seek_table_buf_sz, size_t uncompressed_size,
    std::unique_ptr<SeekableDecompressor>* out) {
  auto decompressor = std::make_unique<SeekableChunkedDecompressor>();
  chunked_compression::HeaderReader reader;
  Status status = reader.Parse(seek_table_buf, seek_table_buf_sz, uncompressed_size,
                               &decompressor->seek_table_);
  if (status != chunked_compression::kStatusOk) {
    return ToZxStatus(status);
  }
  *out = std::move(decompressor);
  return ZX_OK;
}

zx_status_t SeekableChunkedDecompressor::DecompressRange(void* uncompressed_buf,
                                                         size_t* uncompressed_size,
                                                         const void* compressed_buf,
                                                         size_t max_compressed_size,
                                                         size_t offset) {
  TRACE_DURATION("blobfs", "SeekableChunkedCompressor::DecompressRange", "length",
                 *uncompressed_size);
  if (*uncompressed_size == 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  std::optional<unsigned> first_idx = seek_table_.EntryForDecompressedOffset(offset);
  std::optional<unsigned> last_idx =
      seek_table_.EntryForDecompressedOffset(offset + (*uncompressed_size) - 1);
  if (!first_idx || !last_idx) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  size_t src_offset = 0;
  size_t dst_offset = 0;
  for (unsigned i = *first_idx; i <= *last_idx; ++i) {
    const chunked_compression::SeekTableEntry& entry = seek_table_.Entries()[i];

    ZX_DEBUG_ASSERT(src_offset + entry.compressed_size <= max_compressed_size);
    ZX_DEBUG_ASSERT(dst_offset + entry.decompressed_size <= *uncompressed_size);

    const uint8_t* src = static_cast<const uint8_t*>(compressed_buf) + src_offset;
    uint8_t* dst = static_cast<uint8_t*>(uncompressed_buf) + dst_offset;
    size_t bytes_in_frame;
    chunked_compression::Status status =
        decompressor_.DecompressFrame(seek_table_, i, src, max_compressed_size - src_offset, dst,
                                      *uncompressed_size - dst_offset, &bytes_in_frame);
    if (status != chunked_compression::kStatusOk) {
      FS_TRACE_ERROR("blobfs DecompressFrame failed: %s\n",
                     zx_status_get_string(ToZxStatus(status)));
      return ToZxStatus(status);
    }
    src_offset += entry.compressed_size;
    dst_offset += bytes_in_frame;
  }
  ZX_ASSERT(dst_offset == *uncompressed_size);
  return ZX_OK;
}

zx::status<CompressionMapping> SeekableChunkedDecompressor::MappingForDecompressedRange(
    size_t offset, size_t len) {
  std::optional<unsigned> first_idx = seek_table_.EntryForDecompressedOffset(offset);
  std::optional<unsigned> last_idx = seek_table_.EntryForDecompressedOffset(offset + len - 1);
  if (!first_idx || !last_idx) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }

  const chunked_compression::SeekTableEntry& first_entry = seek_table_.Entries()[*first_idx];
  const chunked_compression::SeekTableEntry& last_entry = seek_table_.Entries()[*last_idx];
  size_t compressed_end = last_entry.compressed_offset + last_entry.compressed_size;
  size_t decompressed_end = last_entry.decompressed_offset + last_entry.decompressed_size;
  if (compressed_end < first_entry.compressed_offset
      || decompressed_end < first_entry.decompressed_offset) {
    // This likely indicates that the seek table was tampered. (Benign corruption would be caught by
    // the header checksum, which is verified during header parsing.)
    // Note that this condition is also checked by the underlying compression library during
    // parsing, but we defensively check it here as well to prevent underflow.
    return zx::error(ZX_ERR_IO_DATA_INTEGRITY);
  }
  return zx::ok(CompressionMapping {
      .compressed_offset = first_entry.compressed_offset,
      .compressed_length = compressed_end - first_entry.compressed_offset,
      .decompressed_offset = first_entry.decompressed_offset,
      .decompressed_length = decompressed_end - first_entry.decompressed_offset,
  });
}

}  // namespace blobfs
