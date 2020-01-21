// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zstd-rac.h"

#include <zircon/types.h>

#include <memory>

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

constexpr int kSeekableCompressionLevel = 18;
constexpr int kSeekableChecksumFlag = 1;
constexpr unsigned kSeekableMaxFrameSize = 4 * kBlobfsBlockSize;

ZSTDSeekableCompressor::ZSTDSeekableCompressor(ZSTD_seekable_CStream* stream,
                                               void* compressed_buffer,
                                               size_t compressed_buffer_length)
    : stream_(stream),
      output_({
          .dst = compressed_buffer,
          .size = compressed_buffer_length,
          // Initialize output buffer leaving space for archive size header.
          .pos = sizeof(uint64_t),
      }) {}

ZSTDSeekableCompressor::~ZSTDSeekableCompressor() { ZSTD_seekable_freeCStream(stream_); }

zx_status_t ZSTDSeekableCompressor::Create(size_t input_size, void* compression_buffer,
                                           size_t compression_buffer_length,
                                           std::unique_ptr<ZSTDSeekableCompressor>* out) {
  if (BufferMax(input_size) > compression_buffer_length)
    return ZX_ERR_BUFFER_TOO_SMALL;

  ZSTD_seekable_CStream* stream = ZSTD_seekable_createCStream();
  if (stream == nullptr)
    return ZX_ERR_NO_MEMORY;

  auto compressor = std::unique_ptr<ZSTDSeekableCompressor>(
      new ZSTDSeekableCompressor(std::move(stream), compression_buffer, compression_buffer_length));

  size_t r = ZSTD_seekable_initCStream(compressor->stream_, kSeekableCompressionLevel,
                                       kSeekableChecksumFlag, kSeekableMaxFrameSize);
  if (ZSTD_isError(r)) {
    FS_TRACE_ERROR("[blobfs][zstd-rac] Failed to initialize seekable cstream: %s\n",
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
  return sizeof(uint64_t) + ZSTD_compressBound(blob_size);
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
      FS_TRACE_ERROR("[blobfs][zstd-rac] Failed to compress in seekable format: %s\n",
                     ZSTD_getErrorName(zstd_return));
      return ZX_ERR_IO_DATA_INTEGRITY;
    }
  }

  return ZX_OK;
}

zx_status_t ZSTDSeekableCompressor::End() {
  size_t zstd_return = ZSTD_seekable_endStream(stream_, &output_);
  if (ZSTD_isError(zstd_return)) {
    FS_TRACE_ERROR("[blobfs][zstd-rac] Failed to end seekable stream: %s\n",
                   ZSTD_getErrorName(zstd_return));
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  // Store archive size header as first bytes of blob.
  uint64_t zstd_archive_size = output_.pos - sizeof(uint64_t);
  uint64_t* size_header = static_cast<uint64_t*>(output_.dst);
  size_header[0] = zstd_archive_size;

  return ZX_OK;
}

size_t ZSTDSeekableCompressor::Size() const { return output_.pos; }

zx_status_t ZSTDSeekableDecompress(void* target_buf, size_t* target_size, const void* src_buf) {
  TRACE_DURATION("blobfs", "ZSTDSeekableDecompress", "target_size", *target_size);
  ZSTD_seekable* stream = ZSTD_seekable_create();
  auto cleanup = fbl::MakeAutoCall([&stream] { ZSTD_seekable_free(stream); });

  // Read archive size header from first bytes of blob.
  const uint64_t* size_header = static_cast<const uint64_t*>(src_buf);
  const uint64_t zstd_archive_size = size_header[0];
  const uint8_t* src_byte_buf = static_cast<const uint8_t*>(src_buf);

  size_t zstd_return =
      ZSTD_seekable_initBuff(stream, &src_byte_buf[sizeof(uint64_t)], zstd_archive_size);
  if (ZSTD_isError(zstd_return)) {
    FS_TRACE_ERROR("[blobfs][zstd-rac] Failed to initialize seekable dstream: %s\n",
                   ZSTD_getErrorName(zstd_return));
    return ZX_ERR_INTERNAL;
  }

  // Do not pass zero length buffers decompression routines.
  if (zstd_archive_size == 0 || *target_size == 0)
    return ZX_ERR_INVALID_ARGS;

  size_t decompressed = 0;
  zstd_return = 0;
  do {
    zstd_return = ZSTD_seekable_decompress(stream, target_buf, *target_size, decompressed);
    decompressed += zstd_return;
    if (ZSTD_isError(zstd_return)) {
      FS_TRACE_ERROR("[blobfs][zstd-rac] Failed to decompress: %s\n",
                     ZSTD_getErrorName(zstd_return));
      return ZX_ERR_IO_DATA_INTEGRITY;
    }
    // From the ZSTD_seekable_decompress Documentation:
    //   The return value is the number of bytes decompressed, or an error code checkable with
    //   ZSTD_isError().
    // Assume that a return value of 0 indicates, not only that 0 bytes were decompressed, but also
    // that there are no more bytes to decompress.
  } while (zstd_return > 0 && decompressed < *target_size);

  *target_size = decompressed;
  return ZX_OK;
}

}  // namespace blobfs
