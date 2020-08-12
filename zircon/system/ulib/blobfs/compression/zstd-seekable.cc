// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zstd-seekable.h"

#include <zircon/types.h>

#include <memory>

#include <blobfs/compression-settings.h>
#include <blobfs/format.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/macros.h>
#include <fs/trace.h>
#include <zstd/zstd.h>
#include <zstd/zstd_seekable.h>

#include "compressor.h"
#include "zircon/errors.h"

namespace blobfs {

constexpr int kDefaultCompressionLevel = 5;

// TODO(fxbug.dev/49551): Consider disabling checksums if cryptographic verification suffices.
constexpr int kSeekableChecksumFlag = 1;

ZSTDSeekableCompressor::ZSTDSeekableCompressor(ZSTD_seekable_CStream* stream,
                                               void* compressed_buffer,
                                               size_t compressed_buffer_length)
    : stream_(stream),
      output_({
          .dst = compressed_buffer,
          .size = compressed_buffer_length,
          // Initialize output buffer leaving space for archive size header.
          .pos = kZSTDSeekableHeaderSize,
      }) {}

ZSTDSeekableCompressor::~ZSTDSeekableCompressor() { ZSTD_seekable_freeCStream(stream_); }

zx_status_t ZSTDSeekableCompressor::Create(CompressionSettings settings, size_t input_size,
                                           void* compression_buffer,
                                           size_t compression_buffer_length,
                                           std::unique_ptr<ZSTDSeekableCompressor>* out) {
  ZX_DEBUG_ASSERT(settings.compression_algorithm == CompressionAlgorithm::ZSTD_SEEKABLE);
  if (BufferMax(input_size) > compression_buffer_length)
    return ZX_ERR_BUFFER_TOO_SMALL;

  ZSTD_seekable_CStream* stream = ZSTD_seekable_createCStream();
  if (stream == nullptr)
    return ZX_ERR_NO_MEMORY;

  auto compressor = std::unique_ptr<ZSTDSeekableCompressor>(
      new ZSTDSeekableCompressor(std::move(stream), compression_buffer, compression_buffer_length));

  int level = settings.compression_level ? *(settings.compression_level) : kDefaultCompressionLevel;
  size_t r = ZSTD_seekable_initCStream(compressor->stream_, level, kSeekableChecksumFlag,
                                       kZSTDSeekableMaxFrameSize);
  if (ZSTD_isError(r)) {
    FS_TRACE_ERROR("[blobfs][zstd-seekable] Failed to initialize seekable cstream: %s\n",
                   ZSTD_getErrorName(r));
    return ZX_ERR_INTERNAL;
  }

  *out = std::move(compressor);
  return ZX_OK;
}

// TODO(markdittmer): This doesn't take into account a couple issues related to the seekable format:
// 1. It doesn't include the seekable format footer.
// 2. Frequent flushes caused by the seekable format's max frame size can cause compressed contents
//    to exceed this bound.
size_t ZSTDSeekableCompressor::BufferMax(size_t blob_size) {
  // Add archive size header to estimate.
  return kZSTDSeekableHeaderSize + ZSTD_compressBound(blob_size);
}

zx_status_t ZSTDSeekableCompressor::WriteHeader(void* buf, size_t buf_size,
                                                ZSTDSeekableHeader header) {
  if (buf_size < kZSTDSeekableHeaderSize) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  uint64_t* size_header = static_cast<uint64_t*>(buf);
  size_header[0] = header.archive_size;

  return ZX_OK;
}

zx_status_t ZSTDSeekableCompressor::Update(const void* input_data, size_t input_length) {
  ZSTD_inBuffer input;
  input.src = input_data;
  input.size = input_length;
  input.pos = 0;

  // Invoke ZSTD_seekable_compressStream repeatedly to consume entire input buffer.
  //
  // From the ZSTD seekable format documentation:
  //   Use ZSTD_seekable_compressStream() repetitively to consume input stream.
  //   The function will automatically update both `pos` fields.
  //   Note that it may not consume the entire input, in which case `pos < size`,
  //   and it's up to the caller to present again remaining data.
  size_t zstd_return = 0;
  while (input.pos != input_length) {
    zstd_return = ZSTD_seekable_compressStream(stream_, &output_, &input);
    if (ZSTD_isError(zstd_return)) {
      FS_TRACE_ERROR("[blobfs][zstd-seekable] Failed to compress in seekable format: %s\n",
                     ZSTD_getErrorName(zstd_return));
      return ZX_ERR_IO_DATA_INTEGRITY;
    }
  }

  return ZX_OK;
}

zx_status_t ZSTDSeekableCompressor::End() {
  size_t zstd_return = ZSTD_seekable_endStream(stream_, &output_);
  if (ZSTD_isError(zstd_return)) {
    FS_TRACE_ERROR("[blobfs][zstd-seekable] Failed to end seekable stream: %s\n",
                   ZSTD_getErrorName(zstd_return));
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  // Store archive size header as first bytes of blob.
  uint64_t archive_size = output_.pos - kZSTDSeekableHeaderSize;
  WriteHeader(output_.dst, output_.size, ZSTDSeekableHeader{archive_size});

  return ZX_OK;
}

size_t ZSTDSeekableCompressor::Size() const { return output_.pos; }

zx_status_t ZSTDSeekableDecompressor::DecompressArchive(void* uncompressed_buf,
                                                        size_t* uncompressed_size,
                                                        const void* compressed_buf,
                                                        size_t compressed_size, size_t offset) {
  ZSTD_seekable* stream = ZSTD_seekable_create();
  auto cleanup = fbl::MakeAutoCall([&stream] { ZSTD_seekable_free(stream); });
  size_t zstd_return = ZSTD_seekable_initBuff(stream, compressed_buf, compressed_size);
  if (ZSTD_isError(zstd_return)) {
    FS_TRACE_ERROR("[blobfs][zstd-seekable] Failed to initialize seekable dstream: %s\n",
                   ZSTD_getErrorName(zstd_return));
    return ZX_ERR_INTERNAL;
  }

  size_t decompressed = 0;
  zstd_return = 0;
  do {
    zstd_return = ZSTD_seekable_decompress(stream, uncompressed_buf, *uncompressed_size,
                                           offset + decompressed);
    decompressed += zstd_return;
    if (ZSTD_isError(zstd_return)) {
      FS_TRACE_ERROR("[blobfs][zstd-seekable] Failed to decompress: %s\n",
                     ZSTD_getErrorName(zstd_return));
      return ZX_ERR_IO_DATA_INTEGRITY;
    }
    // From the ZSTD_seekable_decompress Documentation:
    //   The return value is the number of bytes decompressed, or an error code checkable with
    //   ZSTD_isError().
    // Assume that a return value of 0 indicates, not only that 0 bytes were decompressed, but also
    // that there are no more bytes to decompress.
  } while (zstd_return > 0 && decompressed < *uncompressed_size);

  *uncompressed_size = decompressed;
  return ZX_OK;
}

zx_status_t ZSTDSeekableDecompressor::Decompress(void* uncompressed_buf, size_t* uncompressed_size,
                                                 const void* compressed_buf,
                                                 const size_t max_compressed_size) {
  return DecompressRange(uncompressed_buf, uncompressed_size, compressed_buf, max_compressed_size,
                         0);
}

// SeekableDecompressor implementation.
zx_status_t ZSTDSeekableDecompressor::DecompressRange(void* uncompressed_buf,
                                                      size_t* uncompressed_size,
                                                      const void* compressed_buf,
                                                      size_t max_compressed_size, size_t offset) {
  TRACE_DURATION("blobfs", "ZSTDSeekableDecompressor::DecompressRange", "uncompressed_size",
                 *uncompressed_size, "max_compressed_size", max_compressed_size);

  ZSTDSeekableHeader header;
  zx_status_t status = ReadHeader(compressed_buf, max_compressed_size, &header);
  if (status != ZX_OK) {
    return status;
  }

  const uint8_t* compressed_byte_buf = static_cast<const uint8_t*>(compressed_buf);
  return DecompressArchive(uncompressed_buf, uncompressed_size,
                           compressed_byte_buf + kZSTDSeekableHeaderSize, header.archive_size,
                           offset);
}

zx_status_t ZSTDSeekableDecompressor::ReadHeader(const void* buf, size_t buf_size,
                                                 ZSTDSeekableHeader* header) {
  if (buf_size < kZSTDSeekableHeaderSize) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  const uint64_t* size_header = static_cast<const uint64_t*>(buf);
  const uint64_t archive_size = size_header[0];
  header->archive_size = archive_size;

  return ZX_OK;
}

}  // namespace blobfs
