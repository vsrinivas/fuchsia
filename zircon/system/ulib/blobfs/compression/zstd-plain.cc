// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zstd-plain.h"

#include <zircon/types.h>

#include <memory>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/macros.h>
#include <fs/trace.h>
#include <zstd/zstd.h>

#include "compressor.h"
#include "zircon/errors.h"

namespace blobfs {

constexpr int kCompressionLevel = 3;

ZSTDCompressor::ZSTDCompressor(ZSTD_CCtx* stream, void* compressed_buffer,
                               size_t compressed_buffer_length)
    : stream_(stream) {
  output_.dst = compressed_buffer;
  output_.size = compressed_buffer_length;
  output_.pos = 0;
}

ZSTDCompressor::~ZSTDCompressor() { ZSTD_freeCStream(stream_); }

zx_status_t ZSTDCompressor::Create(size_t input_size, void* compression_buffer,
                                   size_t compression_buffer_length,
                                   std::unique_ptr<ZSTDCompressor>* out) {
  if (BufferMax(input_size) > compression_buffer_length) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  ZSTD_CStream* stream = ZSTD_createCStream();
  if (stream == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  auto compressor = std::unique_ptr<ZSTDCompressor>(
      new ZSTDCompressor(stream, compression_buffer, compression_buffer_length));

  ssize_t r = ZSTD_initCStream(compressor->stream_, kCompressionLevel);
  if (ZSTD_isError(r)) {
    FS_TRACE_ERROR("[blobfs][zstd] Failed to initialize cstream: %s\n", ZSTD_getErrorName(r));
    return ZX_ERR_INTERNAL;
  }

  *out = std::move(compressor);
  return ZX_OK;
}

size_t ZSTDCompressor::BufferMax(size_t blob_size) { return ZSTD_compressBound(blob_size); }

zx_status_t ZSTDCompressor::Update(const void* input_data, size_t input_length) {
  ZSTD_inBuffer input;
  input.src = input_data;
  input.size = input_length;
  input.pos = 0;

  size_t r = ZSTD_compressStream(stream_, &output_, &input);
  if (ZSTD_isError(r)) {
    FS_TRACE_ERROR("[blobfs][zstd] Failed to compress: %s\n", ZSTD_getErrorName(r));
    return ZX_ERR_IO_DATA_INTEGRITY;
  } else if (input.pos != input_length) {
    // The only way this condition can occur is when the output buffer is full.
    //
    // From the ZSTD documentation:
    //   Note that the function may not consume the entire input, for example, because the
    //   output buffer is already full, in which case `input.pos < input.size`.
    //
    // If this is the case, a client must have not supplied an honest value for
    // |input_size| when creating the ZSTDCompressor object, which requires that the
    // output compression buffer be large enough to hold the "worst case" input size.
    FS_TRACE_ERROR("[blobfs][zstd] Could not compress all input\n");
    return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

zx_status_t ZSTDCompressor::End() {
  size_t r = ZSTD_flushStream(stream_, &output_);
  if (ZSTD_isError(r)) {
    FS_TRACE_ERROR("[blobfs][zstd] Failed to flush stream: %s\n", ZSTD_getErrorName(r));
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  r = ZSTD_endStream(stream_, &output_);
  if (ZSTD_isError(r)) {
    FS_TRACE_ERROR("[blobfs][zstd] Failed to end stream: %s\n", ZSTD_getErrorName(r));
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  return ZX_OK;
}

size_t ZSTDCompressor::Size() const { return output_.pos; }

zx_status_t ZSTDDecompress(void* target_buf, size_t* target_size, const void* src_buf,
                           size_t* src_size) {
  TRACE_DURATION("blobfs", "ZSTDDecompress", "target_size", *target_size, "src_size", *src_size);
  ZSTD_DStream* stream = ZSTD_createDStream();
  auto cleanup = fbl::MakeAutoCall([&stream] { ZSTD_freeDStream(stream); });

  size_t r = ZSTD_initDStream(stream);
  if (ZSTD_isError(r)) {
    FS_TRACE_ERROR("[blobfs][zstd] Failed to initialize dstream: %s\n", ZSTD_getErrorName(r));
    return ZX_ERR_INTERNAL;
  }

  // Passing zero length buffers to ZSTD_decompress will cause an infinite loop.
  if (*src_size == 0 || *target_size == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  ZSTD_inBuffer input;
  input.src = src_buf;
  input.size = *src_size;
  input.pos = 0;

  ZSTD_outBuffer output;
  output.dst = target_buf;
  output.size = *target_size;
  output.pos = 0;

  r = 0;
  do {
    r = ZSTD_decompressStream(stream, &output, &input);
    if (ZSTD_isError(r)) {
      FS_TRACE_ERROR("[blobfs][zstd] Failed to decompress: %s\n", ZSTD_getErrorName(r));
      return ZX_ERR_IO_DATA_INTEGRITY;
    }
    // Paraphrasing from the ZSTD Documentation:
    // For any return value > 0, there is still decoding or flushing to do
    // within the current frame. The return value is a hint for the next input size.
  } while (r > 0);

  *src_size = input.pos;
  *target_size = output.pos;
  return ZX_OK;
}

}  // namespace blobfs
