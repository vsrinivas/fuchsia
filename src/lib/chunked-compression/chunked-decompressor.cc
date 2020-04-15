// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/array.h>
#include <src/lib/chunked-compression/chunked-archive.h>
#include <src/lib/chunked-compression/chunked-decompressor.h>
#include <src/lib/chunked-compression/status.h>
#include <src/lib/fxl/logging.h>
#include <zstd/zstd.h>

namespace chunked_compression {

struct ChunkedDecompressor::DecompressionContext {
  DecompressionContext() = default;
  explicit DecompressionContext(ZSTD_DCtx* ctx) : inner_(ctx) {}
  ~DecompressionContext() { ZSTD_freeDCtx(inner_); }

  ZSTD_DCtx* inner_;
};

ChunkedDecompressor::ChunkedDecompressor()
    : context_(std::make_unique<DecompressionContext>(ZSTD_createDCtx())) {}
ChunkedDecompressor::~ChunkedDecompressor() {}

Status ChunkedDecompressor::DecompressBytes(const void* data, size_t len,
                                            fbl::Array<uint8_t>* data_out,
                                            size_t* bytes_written_out) {
  Status status;
  SeekTable table;
  HeaderReader reader;
  if ((status = reader.Parse(data, len, len, &table)) != kStatusOk) {
    FXL_LOG(ERROR) << "Failed to parse table";
    return status;
  }
  ChunkedDecompressor decompressor;
  size_t out_len = table.DecompressedSize();
  fbl::Array<uint8_t> buf(new uint8_t[out_len], out_len);
  status = decompressor.Decompress(table, data, len, buf.get(), buf.size(), bytes_written_out);
  if (status == kStatusOk) {
    *data_out = std::move(buf);
  }
  return status;
}

Status ChunkedDecompressor::Decompress(const SeekTable& table, const void* data, size_t len,
                                       void* dst, size_t dst_len, size_t* bytes_written_out) {
  Status status;
  if (dst_len < table.DecompressedSize() || len < table.CompressedSize()) {
    return kStatusErrBufferTooSmall;
  }

  size_t bytes_written = 0;
  for (unsigned i = 0; i < table.Entries().size(); ++i) {
    const SeekTableEntry& entry = table.Entries()[i];
    ZX_DEBUG_ASSERT(entry.compressed_offset + entry.compressed_size <= len);
    ZX_DEBUG_ASSERT(entry.decompressed_offset + entry.decompressed_size <= dst_len);
    auto frame_src = (static_cast<const uint8_t*>(data) + entry.compressed_offset);
    auto frame_dst = (static_cast<uint8_t*>(dst) + entry.decompressed_offset);
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

Status ChunkedDecompressor::DecompressFrame(const SeekTable& table, unsigned frame_num,
                                            const void* compressed_buffer,
                                            size_t compressed_buffer_len, void* dst,
                                            size_t dst_len, size_t* bytes_written_out) {
  if (frame_num >= table.Entries().size()) {
    return kStatusErrInvalidArgs;
  }
  const SeekTableEntry& entry = table.Entries()[frame_num];
  if (compressed_buffer_len < entry.compressed_size || dst_len < entry.decompressed_size) {
    return kStatusErrBufferTooSmall;
  }

  size_t decompressed_size = ZSTD_decompressDCtx(context_->inner_, dst, entry.decompressed_size,
                                                 compressed_buffer, entry.compressed_size);
  if (ZSTD_isError(decompressed_size)) {
    FXL_LOG(ERROR) << "Decompression failed: " << ZSTD_getErrorName(decompressed_size);
    return kStatusErrInternal;
  }

  if (decompressed_size != entry.decompressed_size) {
    FXL_LOG(ERROR) << "Decompressed " << decompressed_size << " bytes, expected "
                   << entry.decompressed_size;
    return kStatusErrIoDataIntegrity;
  }

  *bytes_written_out = decompressed_size;
  return kStatusOk;
}

}  // namespace chunked_compression
