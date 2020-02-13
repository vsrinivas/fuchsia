// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lz4.h"

#include <zircon/types.h>

#include <memory>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/macros.h>
#include <fs/trace.h>
#include <lz4/lz4frame.h>

#include "compressor.h"
#include "decompressor.h"

namespace blobfs {

constexpr size_t kLz4HeaderSize = 15;

LZ4Compressor::LZ4Compressor(LZ4F_compressionContext_t ctx, void* compression_buffer,
                             size_t compression_buffer_length)
    : ctx_(std::move(ctx)),
      buf_(compression_buffer),
      buf_max_(compression_buffer_length),
      buf_used_(0) {}

void* LZ4Compressor::Buffer() const {
  return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(buf_) + buf_used_);
}

size_t LZ4Compressor::Remaining() const { return buf_max_ - buf_used_; }

LZ4Compressor::~LZ4Compressor() { LZ4F_freeCompressionContext(ctx_); }

zx_status_t LZ4Compressor::Create(size_t input_size, void* compression_buffer,
                                  size_t compression_buffer_length,
                                  std::unique_ptr<LZ4Compressor>* out) {
  if (BufferMax(input_size) > compression_buffer_length) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  LZ4F_compressionContext_t ctx;
  LZ4F_errorCode_t errc = LZ4F_createCompressionContext(&ctx, LZ4F_VERSION);
  if (LZ4F_isError(errc)) {
    return ZX_ERR_NO_MEMORY;
  }

  auto compressor = std::unique_ptr<LZ4Compressor>(
      new LZ4Compressor(std::move(ctx), compression_buffer, compression_buffer_length));
  size_t r =
      LZ4F_compressBegin(compressor->ctx_, compressor->Buffer(), compressor->Remaining(), nullptr);
  if (LZ4F_isError(r)) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  compressor->buf_used_ += r;

  *out = std::move(compressor);
  return ZX_OK;
}

size_t LZ4Compressor::BufferMax(size_t blob_size) {
  return kLz4HeaderSize + LZ4F_compressBound(blob_size, nullptr);
}

zx_status_t LZ4Compressor::Update(const void* data, size_t length) {
  size_t r = LZ4F_compressUpdate(ctx_, Buffer(), Remaining(), data, length, nullptr);
  if (LZ4F_isError(r)) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  buf_used_ += r;
  return ZX_OK;
}

zx_status_t LZ4Compressor::End() {
  size_t r = LZ4F_compressEnd(ctx_, Buffer(), Remaining(), nullptr);
  if (LZ4F_isError(r)) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  buf_used_ += r;
  return ZX_OK;
}

size_t LZ4Compressor::Size() const { return buf_used_; }

zx_status_t LZ4Decompressor::Decompress(void* uncompressed_buf_, size_t* uncompressed_size,
                                        const void* compressed_buf_,
                                        const size_t max_compressed_size) {
  TRACE_DURATION("blobfs", "LZ4Decompressor::Decompress", "uncompressed_size", *uncompressed_size,
                 "compressed_size", max_compressed_size);
  uint8_t* uncompressed_buf = reinterpret_cast<uint8_t*>(uncompressed_buf_);
  const uint8_t* compressed_buf = reinterpret_cast<const uint8_t*>(compressed_buf_);

  LZ4F_decompressionContext_t ctx;
  LZ4F_errorCode_t errc = LZ4F_createDecompressionContext(&ctx, LZ4F_VERSION);
  if (LZ4F_isError(errc)) {
    return ZX_ERR_NO_MEMORY;
  }

  auto cleanup = fbl::MakeAutoCall([&ctx]() { LZ4F_freeDecompressionContext(ctx); });
  size_t target_drained = 0;
  size_t src_drained = 0;

  // Decompress the first four bytes of the source without consuming the
  // destination buffer to determine the size of the frame header.
  size_t dst_sz_next = 0;
  size_t src_sz_next = 4;

  while (true) {
    uint8_t* target = uncompressed_buf + target_drained;
    const uint8_t* src = compressed_buf + src_drained;

    size_t r = LZ4F_decompress(ctx, target, &dst_sz_next, src, &src_sz_next, nullptr);
    if (LZ4F_isError(r)) {
      return ZX_ERR_IO_DATA_INTEGRITY;
    }

    // After the call to decompress, these are the sizes which were used.
    target_drained += dst_sz_next;
    src_drained += src_sz_next;

    if (r == 0) {
      break;
    }

    dst_sz_next = *uncompressed_size - target_drained;
    src_sz_next = r;
  }

  *uncompressed_size = target_drained;
  return ZX_OK;
}

}  // namespace blobfs
