// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/compression/chunked.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/status.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <src/lib/chunked-compression/chunked-archive.h>
#include <src/lib/chunked-compression/status.h>
#include <src/lib/chunked-compression/streaming-chunked-compressor.h>

#include "src/lib/storage/vfs/cpp/trace.h"
#include "src/storage/blobfs/compression/configs/chunked_compression_params.h"
#include "src/storage/blobfs/compression_settings.h"

namespace blobfs {

namespace {

using ::chunked_compression::CompressionParams;
using ::chunked_compression::Status;
using ::chunked_compression::ToZxStatus;

}  // namespace

// ChunkedCompressor

ChunkedCompressor::ChunkedCompressor(chunked_compression::StreamingChunkedCompressor compressor,
                                     size_t input_len)
    : compressor_(std::move(compressor)), input_len_(input_len) {}

zx_status_t ChunkedCompressor::Create(CompressionSettings settings, size_t input_size,
                                      size_t* output_limit_out,
                                      std::unique_ptr<ChunkedCompressor>* out) {
  ZX_DEBUG_ASSERT(settings.compression_algorithm == CompressionAlgorithm::kChunked);
  CompressionParams params = GetDefaultChunkedCompressionParams(input_size);
  if (settings.compression_level) {
    params.compression_level = *(settings.compression_level);
  }

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
  Status status = compressor_.Init(input_len_, dst, dst_len);
  if (status != chunked_compression::kStatusOk) {
    zx_status_t zstatus = ToZxStatus(status);
    FX_LOGS(ERROR) << "Failed to initialize compressor: " << zstatus;
    return zstatus;
  }
  return ZX_OK;
}

size_t ChunkedCompressor::BufferMax(size_t input_length) {
  chunked_compression::CompressionParams params = GetDefaultChunkedCompressionParams(input_length);
  return params.ComputeOutputSizeLimit(input_length);
}

size_t ChunkedCompressor::Size() const { return compressed_size_.value_or(0ul); }

zx_status_t ChunkedCompressor::Update(const void* input_data, size_t input_length) {
  TRACE_DURATION("blobfs", "ChunkedCompressor::Update", "input_length", input_length);
  if (compressor_.Update(input_data, input_length) != chunked_compression::kStatusOk) {
    FX_LOGS(ERROR) << "Compression failed.";
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

zx_status_t ChunkedCompressor::End() {
  TRACE_DURATION("blobfs", "ChunkedCompressor::End");
  size_t sz;
  Status status = compressor_.Final(&sz);
  if (status != chunked_compression::kStatusOk) {
    FX_LOGS(ERROR) << "Compression failed.";
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
    FX_LOGS(ERROR) << "Invalid archive header.";
    return ToZxStatus(status);
  }
  size_t decompression_buf_size = *uncompressed_size;
  chunked_compression::ChunkedDecompressor decompressor;
  if (decompressor.Decompress(seek_table, compressed_buf, max_compressed_size, uncompressed_buf,
                              decompression_buf_size,
                              uncompressed_size) != chunked_compression::kStatusOk) {
    FX_LOGS(ERROR) << "Failed to decompress archive.";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  return ZX_OK;
}

// SeekableChunkedDecompressor

SeekableChunkedDecompressor::SeekableChunkedDecompressor(
    std::unique_ptr<chunked_compression::SeekTable> seek_table)
    : seek_table_(std::move(seek_table)) {}

zx_status_t SeekableChunkedDecompressor::CreateDecompressor(
    cpp20::span<const uint8_t> seek_table_data, size_t max_compressed_size,
    std::unique_ptr<SeekableDecompressor>* out) {
  auto seek_table = std::make_unique<chunked_compression::SeekTable>();
  chunked_compression::HeaderReader reader;
  Status status = reader.Parse(seek_table_data.begin(), seek_table_data.size(), max_compressed_size,
                               seek_table.get());
  if (status != chunked_compression::kStatusOk) {
    return ToZxStatus(status);
  }
  *out = std::make_unique<SeekableChunkedDecompressor>(std::move(seek_table));
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
  std::optional<unsigned> first_idx = seek_table_->EntryForDecompressedOffset(offset);
  std::optional<unsigned> last_idx =
      seek_table_->EntryForDecompressedOffset(offset + (*uncompressed_size) - 1);
  if (!first_idx || !last_idx) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  size_t src_offset = 0;
  size_t dst_offset = 0;
  chunked_compression::ChunkedDecompressor decompressor;
  for (unsigned i = *first_idx; i <= *last_idx; ++i) {
    const chunked_compression::SeekTableEntry& entry = seek_table_->Entries()[i];

    ZX_DEBUG_ASSERT(src_offset + entry.compressed_size <= max_compressed_size);
    ZX_DEBUG_ASSERT(dst_offset + entry.decompressed_size <= *uncompressed_size);

    const uint8_t* src = static_cast<const uint8_t*>(compressed_buf) + src_offset;
    uint8_t* dst = static_cast<uint8_t*>(uncompressed_buf) + dst_offset;
    size_t bytes_in_frame;
    chunked_compression::Status status =
        decompressor.DecompressFrame(*seek_table_, i, src, max_compressed_size - src_offset, dst,
                                     *uncompressed_size - dst_offset, &bytes_in_frame);
    if (status != chunked_compression::kStatusOk) {
      FX_LOGS(ERROR) << "DecompressFrame failed: " << status;
      return ToZxStatus(status);
    }
    src_offset += entry.compressed_size;
    dst_offset += bytes_in_frame;
  }
  ZX_ASSERT(dst_offset == *uncompressed_size);
  return ZX_OK;
}

zx::result<CompressionMapping> SeekableChunkedDecompressor::MappingForDecompressedRange(
    size_t offset, size_t len, size_t max_decompressed_len) const {
  return MappingForDecompressedRange(*seek_table_, offset, len, max_decompressed_len);
}

zx::result<CompressionMapping> SeekableChunkedDecompressor::MappingForDecompressedRange(
    const chunked_compression::SeekTable& seek_table, size_t offset, size_t len,
    size_t max_decompressed_len) {
  if (max_decompressed_len == 0) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  std::optional<unsigned> first_idx = seek_table.EntryForDecompressedOffset(offset);
  std::optional<unsigned> last_idx = seek_table.EntryForDecompressedOffset(offset + len - 1);
  if (!first_idx || !last_idx) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }

  const chunked_compression::SeekTableEntry& first_entry = seek_table.Entries()[*first_idx];
  const chunked_compression::SeekTableEntry& last_entry = seek_table.Entries()[*last_idx];
  size_t compressed_end = last_entry.compressed_offset + last_entry.compressed_size;
  size_t decompressed_end = last_entry.decompressed_offset + last_entry.decompressed_size;
  if (compressed_end < first_entry.compressed_offset ||
      decompressed_end < first_entry.decompressed_offset) {
    // This likely indicates that the seek table was tampered. (Benign corruption would be caught by
    // the header checksum, which is verified during header parsing.)
    // Note that this condition is also checked by the underlying compression library during
    // parsing, but we defensively check it here as well to prevent underflow.
    FX_LOGS(ERROR) << "Seek table may be corrupted when checking underflow";
    return zx::error(ZX_ERR_IO_DATA_INTEGRITY);
  }

  // Return the computed range if its size falls within max_decompressed_len.
  if (likely(decompressed_end - first_entry.decompressed_offset <= max_decompressed_len)) {
    return zx::ok(CompressionMapping{
        .compressed_offset = first_entry.compressed_offset,
        .compressed_length = compressed_end - first_entry.compressed_offset,
        .decompressed_offset = first_entry.decompressed_offset,
        .decompressed_length = decompressed_end - first_entry.decompressed_offset,
    });
  }

  size_t max_decompressed_end;
  if (add_overflow(first_entry.decompressed_offset, max_decompressed_len, &max_decompressed_end)) {
    // We're here because (decompressed_end - first_entry.decompressed_offset) is larger than
    // max_decompressed_len. So by definition first_entry.decompressed_offset + max_decompressed_len
    // cannot result in an overflow, as we know that decompressed_end is valid. This likely
    // indicates some kind of corruption in the seek table.
    FX_LOGS(ERROR) << "Seek table may be corrupted when checking overflow";
    return zx::error(ZX_ERR_IO_DATA_INTEGRITY);
  }

  // Start at the entry that contains the offset (max_decompressed_end - 1) and work backwards until
  // we hit the required size constraint.
  std::optional<unsigned> max_idx = seek_table.EntryForDecompressedOffset(max_decompressed_end - 1);
  if (!max_idx) {
    // This again cannot happen for similar reasons as the overflow check above.
    FX_LOGS(ERROR) << "Seek table may be corrupted when finding compression offset";
    return zx::error(ZX_ERR_IO_DATA_INTEGRITY);
  }
  unsigned idx = *max_idx;
  while (idx >= first_idx) {
    const chunked_compression::SeekTableEntry& max_entry = seek_table.Entries()[idx];
    compressed_end = max_entry.compressed_offset + max_entry.compressed_size;
    decompressed_end = max_entry.decompressed_offset + max_entry.decompressed_size;
    if (decompressed_end <= max_decompressed_end) {
      return zx::ok(CompressionMapping{
          .compressed_offset = first_entry.compressed_offset,
          .compressed_length = compressed_end - first_entry.compressed_offset,
          .decompressed_offset = first_entry.decompressed_offset,
          .decompressed_length = decompressed_end - first_entry.decompressed_offset,
      });
    }
    if (idx == 0) {
      break;
    }
    --idx;
  }
  // We cannot accommodate even a single entry within max_decompressed_len.
  return zx::error(ZX_ERR_OUT_OF_RANGE);
}

}  // namespace blobfs
