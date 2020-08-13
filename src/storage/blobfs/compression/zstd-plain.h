// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_COMPRESSION_ZSTD_PLAIN_H_
#define SRC_STORAGE_BLOBFS_COMPRESSION_ZSTD_PLAIN_H_

#include <zircon/types.h>

#include <memory>

#include <blobfs/compression-settings.h>
#include <blobfs/format.h>
#include <zstd/zstd.h>

#include "compressor.h"
#include "decompressor.h"

namespace blobfs {

class ZSTDCompressor : public Compressor {
 public:
  static uint32_t InodeHeaderCompressionFlags() { return kBlobFlagZSTDCompressed; }

  // Returns the maximum possible size a buffer would need to be
  // in order to compress data of size |input_length|.
  static size_t BufferMax(size_t input_length);

  static zx_status_t Create(CompressionSettings settings, size_t input_size,
                            void* compression_buffer, size_t compression_buffer_length,
                            std::unique_ptr<ZSTDCompressor>* out);
  ~ZSTDCompressor();

  ////////////////////////////////////////
  // Compressor interface
  size_t Size() const final;
  zx_status_t Update(const void* input_data, size_t input_length) final;
  zx_status_t End() final;

 private:
  ZSTDCompressor(ZSTD_CCtx* ctx, void* compression_buffer, size_t compression_buffer_length);

  ZSTD_CCtx* stream_ = nullptr;
  ZSTD_outBuffer output_ = {};
};

class AbstractZSTDDecompressor : public Decompressor {
 public:
  AbstractZSTDDecompressor() = default;
  virtual ~AbstractZSTDDecompressor() {}
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AbstractZSTDDecompressor);

  // Decompressor implementation.
  virtual zx_status_t Decompress(void* uncompressed_buf, size_t* uncompressed_size,
                                 const void* compressed_buf, const size_t max_compressed_size);

 private:
  virtual size_t DecompressStream(ZSTD_DStream* zds, ZSTD_outBuffer* output,
                                  ZSTD_inBuffer* input) const = 0;
};

class ZSTDDecompressor : public AbstractZSTDDecompressor {
 public:
  ZSTDDecompressor() = default;
  DISALLOW_COPY_ASSIGN_AND_MOVE(ZSTDDecompressor);

  // AbstractZSTDDecompressor interface.
  size_t DecompressStream(ZSTD_DStream* zds, ZSTD_outBuffer* output,
                          ZSTD_inBuffer* input) const final;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_COMPRESSION_ZSTD_PLAIN_H_
