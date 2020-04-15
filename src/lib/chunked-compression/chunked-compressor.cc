// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include <algorithm>

#include <fbl/algorithm.h>
#include <src/lib/chunked-compression/chunked-archive.h>
#include <src/lib/chunked-compression/chunked-compressor.h>
#include <src/lib/chunked-compression/status.h>
#include <src/lib/fxl/logging.h>
#include <zstd/zstd.h>

namespace chunked_compression {

CompressionParams::CompressionParams()
    : compression_level(DefaultCompressionLevel()), chunk_size(MinChunkSize()) {}

int CompressionParams::DefaultCompressionLevel() { return 3; }
int CompressionParams::MinCompressionLevel() { return ZSTD_minCLevel(); }
int CompressionParams::MaxCompressionLevel() { return ZSTD_maxCLevel(); }

size_t CompressionParams::ChunkSizeForInputSize(size_t len) {
  if (len <= (1 << 20)) {  // Up to 1M
    return MinChunkSize();
  } else if (len <= (1 << 24)) {  // Up to 16M
    return 262144;                // 256K, or 64 4k pages
  } else if (len <= (1 << 26)) {  // Up to 64M
    return 524288;                // 512K, or 128 4k pages
  } else {
    return MaxChunkSize();
  }
}
size_t CompressionParams::MinChunkSize() { return 131072; /* 128K, or 32 4k pages */ }
size_t CompressionParams::MaxChunkSize() { return 1048576; /* 1M, or 256 4k pages */ }

struct ChunkedCompressor::CompressionContext {
  CompressionContext() = default;
  explicit CompressionContext(ZSTD_CCtx* ctx) : inner_(ctx) {}
  ~CompressionContext() { ZSTD_freeCCtx(inner_); }

  ZSTD_CCtx* inner_;
};

ChunkedCompressor::ChunkedCompressor() : ChunkedCompressor(CompressionParams{}) {}

ChunkedCompressor::ChunkedCompressor(CompressionParams params)
    : params_(params), context_(std::make_unique<CompressionContext>(ZSTD_createCCtx())) {}

ChunkedCompressor::~ChunkedCompressor() {}

Status ChunkedCompressor::CompressBytes(const void* data, size_t data_len,
                                        fbl::Array<uint8_t>* compressed_data_out,
                                        size_t* bytes_written_out) {
  ChunkedCompressor compressor;
  size_t out_len = compressor.ComputeOutputSizeLimit(data_len);
  fbl::Array<uint8_t> buf(new uint8_t[out_len], out_len);
  Status status = compressor.Compress(data, data_len, buf.get(), out_len, bytes_written_out);
  if (status == kStatusOk) {
    *compressed_data_out = std::move(buf);
  }
  return status;
}

size_t ChunkedCompressor::ComputeOutputSizeLimit(size_t len) {
  if (len == 0) {
    return 0ul;
  }
  const size_t num_frames = HeaderWriter::NumFramesForDataSize(len, params_.chunk_size);
  size_t size = HeaderWriter::MetadataSizeForNumFrames(num_frames);
  size += (ZSTD_compressBound(params_.chunk_size) * num_frames);
  return size;
}

Status ChunkedCompressor::Compress(const void* data, size_t data_len, void* dst, size_t dst_len,
                                   size_t* bytes_written_out) {
  if (data_len == 0) {
    *bytes_written_out = 0ul;
    return kStatusOk;
  }
  const size_t num_frames = HeaderWriter::NumFramesForDataSize(data_len, params_.chunk_size);
  const size_t metadata_size = HeaderWriter::MetadataSizeForNumFrames(num_frames);
  ZX_DEBUG_ASSERT(metadata_size < dst_len);

  HeaderWriter header_writer(dst, metadata_size, num_frames);

  Status status;
  size_t bytes_read = 0;
  // The seek table isn't written yet but it is 'allocated' by seeking past the expected size.
  // It will be populated as we compress each frame.
  size_t bytes_written = metadata_size;
  unsigned chunks_written = 0;
  for (size_t chunk_off = 0; chunk_off < data_len; chunk_off += params_.chunk_size) {
    const size_t chunk_len = std::min(params_.chunk_size, data_len - chunk_off);
    ZX_DEBUG_ASSERT(chunks_written < num_frames);
    ZX_DEBUG_ASSERT(chunk_off + chunk_len <= data_len);
    ZX_DEBUG_ASSERT(bytes_written + ZSTD_compressBound(chunk_len) <= dst_len);

    const uint8_t* chunk_start = static_cast<const uint8_t*>(data) + chunk_off;
    uint8_t* frame_output_start = static_cast<uint8_t*>(dst) + bytes_written;
    size_t compressed_chunk_size;
    status = CompressChunk(chunk_start, chunk_len, frame_output_start, dst_len - bytes_written,
                           &compressed_chunk_size);
    if (status != kStatusOk) {
      return status;
    }
    ZX_DEBUG_ASSERT(bytes_written + compressed_chunk_size <= dst_len);

    SeekTableEntry entry;
    entry.decompressed_offset = chunk_off;
    entry.decompressed_size = chunk_len;
    entry.compressed_offset = bytes_written;
    entry.compressed_size = compressed_chunk_size;
    status = header_writer.AddEntry(entry);
    if (status != kStatusOk) {
      FXL_LOG(ERROR) << "Failed to write chunk " << chunks_written;
      return status;
    }

    ++chunks_written;
    bytes_read += chunk_len;
    bytes_written += compressed_chunk_size;

    if (progress_callback_) {
      (*progress_callback_)(bytes_read, data_len, bytes_written);
    }
  };

  ZX_DEBUG_ASSERT(bytes_read == data_len);

  if ((status = header_writer.Finalize()) != kStatusOk) {
    return status;
  }

  *bytes_written_out = bytes_written;
  return kStatusOk;
}

Status ChunkedCompressor::CompressChunk(const void* data, size_t len, void* dst, size_t dst_len,
                                        size_t* bytes_written_out) {
  size_t compressed_size =
      ZSTD_compressCCtx(context_->inner_, dst, dst_len, data, len, params_.compression_level);
  if (ZSTD_isError(compressed_size)) {
    FXL_LOG(ERROR) << "Compression failed: " << ZSTD_getErrorName(compressed_size);
    return kStatusErrInternal;
  }
  *bytes_written_out = compressed_size;
  return kStatusOk;
}

}  // namespace chunked_compression
