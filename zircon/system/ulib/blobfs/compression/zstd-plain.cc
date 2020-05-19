// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zstd-plain.h"

#include <zircon/types.h>

#include <memory>

#include <blobfs/compression-settings.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/macros.h>
#include <fs/trace.h>
#include <zstd/zstd.h>

#include "compressor.h"
#include "zircon/errors.h"

namespace blobfs {

constexpr int kDefaultCompressionLevel = 3;

zx_status_t AbstractZSTDDecompressor::Decompress(void* uncompressed_buf, size_t* uncompressed_size,
                                                 const void* compressed_buf,
                                                 const size_t max_compressed_size) {
  TRACE_DURATION("blobfs", "AbstractZSTDDecompressor::Decompress", "uncompressed_size",
                 *uncompressed_size, "max_compressed_size", max_compressed_size);
  ZSTD_DStream* stream = ZSTD_createDStream();
  auto cleanup = fbl::MakeAutoCall([&stream] { ZSTD_freeDStream(stream); });

  size_t r = ZSTD_initDStream(stream);
  if (ZSTD_isError(r)) {
    FS_TRACE_ERROR("[blobfs][zstd] Failed to initialize dstream: %s\n", ZSTD_getErrorName(r));
    return ZX_ERR_INTERNAL;
  }

  ZSTD_inBuffer input;
  input.src = compressed_buf;
  input.size = max_compressed_size;
  input.pos = 0;

  ZSTD_outBuffer output;
  output.dst = uncompressed_buf;
  output.size = *uncompressed_size;
  output.pos = 0;

  size_t prev_output_pos = 0;
  r = 0;
  do {
    prev_output_pos = output.pos;
    r = DecompressStream(stream, &output, &input);
    if (ZSTD_isError(r)) {
      FS_TRACE_ERROR("[blobfs][zstd] Failed to decompress: %s\n", ZSTD_getErrorName(r));
      return ZX_ERR_IO_DATA_INTEGRITY;
    }
    // Halt decompression when no more progress is being made (or can be made) on the output buffer.
    // Unfortunately, the return value from `ZSTD_decompressStream` cannot be used for this purpose.
    // Paraphrasing from zstd documentation, the return value is one of:
    //   a) 0, indicating that zstd just finished decompressing an entire _frame_ (but not
    //      necessarily the entire archive),
    //   b) an error code (handled by `ZSTD_isError` check above), or
    //   c) suggested next input size, which is _just a hint for better latency_.
    // None of these provides a difinitive signal that the entire archive has been decompressed.
  } while (output.pos < output.size && prev_output_pos != output.pos);

  *uncompressed_size = output.pos;
  return ZX_OK;
}

ZSTDCompressor::ZSTDCompressor(ZSTD_CCtx* stream, void* compressed_buffer,
                               size_t compressed_buffer_length)
    : stream_(stream) {
  output_.dst = compressed_buffer;
  output_.size = compressed_buffer_length;
  output_.pos = 0;
}

ZSTDCompressor::~ZSTDCompressor() { ZSTD_freeCStream(stream_); }

zx_status_t ZSTDCompressor::Create(CompressionSettings settings, size_t input_size,
                                   void* compression_buffer, size_t compression_buffer_length,
                                   std::unique_ptr<ZSTDCompressor>* out) {
  ZX_DEBUG_ASSERT(settings.compression_algorithm == CompressionAlgorithm::ZSTD);
  if (BufferMax(input_size) > compression_buffer_length) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  ZSTD_CStream* stream = ZSTD_createCStream();
  if (stream == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  auto compressor = std::unique_ptr<ZSTDCompressor>(
      new ZSTDCompressor(stream, compression_buffer, compression_buffer_length));

  int level = settings.compression_level ? *(settings.compression_level) : kDefaultCompressionLevel;
  ssize_t r = ZSTD_initCStream(compressor->stream_, level);
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

size_t ZSTDDecompressor::DecompressStream(ZSTD_DStream* zds, ZSTD_outBuffer* output,
                                          ZSTD_inBuffer* input) const {
  return ZSTD_decompressStream(zds, output, input);
}

}  // namespace blobfs
