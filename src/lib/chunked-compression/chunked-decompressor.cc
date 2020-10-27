// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

#include <fbl/array.h>
#include <src/lib/chunked-compression/chunked-archive.h>
#include <src/lib/chunked-compression/chunked-decompressor.h>
#include <src/lib/chunked-compression/status.h>
#include <zstd/zstd.h>
#include <zstd/zstd_errors.h>

namespace chunked_compression {

namespace {

// Returns whether |error_code| indicates a likely data corruption.
bool LikelyCorrupton(ZSTD_ErrorCode error_code) {
  return (error_code == ZSTD_error_checksum_wrong || error_code == ZSTD_error_corruption_detected ||
          error_code == ZSTD_error_prefix_unknown);
}

}  // namespace

struct ChunkedDecompressor::DecompressionContext {
  DecompressionContext() = default;
  explicit DecompressionContext(ZSTD_DCtx* ctx) : inner_(ctx) {}
  ~DecompressionContext() { ZSTD_freeDCtx(inner_); }

  ZSTD_DCtx* inner_;
};

ChunkedDecompressor::ChunkedDecompressor()
    : context_(std::make_unique<DecompressionContext>(ZSTD_createDCtx())) {}
ChunkedDecompressor::~ChunkedDecompressor() {}

Status ChunkedDecompressor::DecompressBytes(const void* input, size_t len,
                                            fbl::Array<uint8_t>* output,
                                            size_t* bytes_written_out) {
  Status status;
  SeekTable table;
  HeaderReader reader;
  if ((status = reader.Parse(input, len, len, &table)) != kStatusOk) {
    FX_LOGS(ERROR) << "Failed to parse table";
    return status;
  }
  ChunkedDecompressor decompressor;
  size_t out_len = table.DecompressedSize();
  fbl::Array<uint8_t> buf(new uint8_t[out_len], out_len);
  status = decompressor.Decompress(table, input, len, buf.get(), buf.size(), bytes_written_out);
  if (status == kStatusOk) {
    *output = std::move(buf);
  }
  return status;
}

Status ChunkedDecompressor::Decompress(const SeekTable& table, const void* input, size_t len,
                                       void* output, size_t output_len, size_t* bytes_written_out) {
  Status status;
  if (output_len < table.DecompressedSize() || len < table.CompressedSize()) {
    return kStatusErrBufferTooSmall;
  }

  size_t bytes_written = 0;
  for (unsigned i = 0; i < table.Entries().size(); ++i) {
    const SeekTableEntry& entry = table.Entries()[i];
    ZX_DEBUG_ASSERT(entry.compressed_offset + entry.compressed_size <= len);
    ZX_DEBUG_ASSERT(entry.decompressed_offset + entry.decompressed_size <= output_len);
    auto frame_src = (static_cast<const uint8_t*>(input) + entry.compressed_offset);
    auto frame_dst = (static_cast<uint8_t*>(output) + entry.decompressed_offset);
    size_t frame_decompressed_size;
    if ((status = DecompressFrame(table, i, frame_src, entry.compressed_size, frame_dst,
                                  entry.decompressed_size, &frame_decompressed_size)) !=
        kStatusOk) {
      return status;
    }
    ZX_DEBUG_ASSERT(frame_decompressed_size == entry.decompressed_size);
    bytes_written += frame_decompressed_size;
  }

  ZX_DEBUG_ASSERT(bytes_written == table.DecompressedSize());
  *bytes_written_out = bytes_written;

  return kStatusOk;
}

Status ChunkedDecompressor::DecompressFrame(const void* compressed_buffer,
                                            size_t compressed_buffer_len, void* dst, size_t dst_len,
                                            size_t* bytes_written_out) {
  size_t decompressed_size = ZSTD_decompressDCtx(context_->inner_, dst, dst_len,
                                                 compressed_buffer, compressed_buffer_len);
  if (ZSTD_isError(decompressed_size)) {
    FX_LOGS(ERROR) << "Decompression failed: " << ZSTD_getErrorName(decompressed_size);
    if (LikelyCorrupton(ZSTD_getErrorCode(decompressed_size))) {
      return kStatusErrIoDataIntegrity;
    }
    return kStatusErrInternal;
  }

  if (decompressed_size != dst_len) {
    FX_LOGS(ERROR) << "Decompressed " << decompressed_size << " bytes, expected "
                   << dst_len;
    return kStatusErrIoDataIntegrity;
  }

  *bytes_written_out = decompressed_size;
  return kStatusOk;
}

Status ChunkedDecompressor::DecompressFrame(const SeekTable& table, unsigned table_index,
                                            const void* compressed_buffer,
                                            size_t compressed_buffer_len, void* dst, size_t dst_len,
                                            size_t* bytes_written_out) {
  if (table_index >= table.Entries().size()) {
    return kStatusErrInvalidArgs;
  }
  const SeekTableEntry& entry = table.Entries()[table_index];
  if (compressed_buffer_len < entry.compressed_size || dst_len < entry.decompressed_size) {
    return kStatusErrBufferTooSmall;
  }

  return DecompressFrame(
      compressed_buffer, entry.compressed_size, dst, entry.decompressed_size, bytes_written_out);
}

}  // namespace chunked_compression
