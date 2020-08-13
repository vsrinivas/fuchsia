// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_COMPRESSION_LZ4_H_
#define SRC_STORAGE_BLOBFS_COMPRESSION_LZ4_H_

#include <zircon/types.h>

#include <memory>

#include <blobfs/format.h>
#include <lz4/lz4frame.h>

#include "compressor.h"
#include "decompressor.h"

namespace blobfs {

class LZ4Compressor : public Compressor {
 public:
  static uint32_t InodeHeaderCompressionFlags() { return kBlobFlagLZ4Compressed; }

  // Returns the maximum possible size a buffer would need to be
  // in order to compress data of size |input_length|.
  static size_t BufferMax(size_t input_length);

  static zx_status_t Create(size_t input_size, void* compression_buffer,
                            size_t compression_buffer_length, std::unique_ptr<LZ4Compressor>* out);
  ~LZ4Compressor();

  ////////////////////////////////////////
  // Compressor interface
  size_t Size() const final;
  zx_status_t Update(const void* input_data, size_t input_length) final;
  zx_status_t End() final;

 private:
  LZ4Compressor(LZ4F_compressionContext_t ctx, void* compression_buffer,
                size_t compression_buffer_length);

  void* Buffer() const;
  size_t Remaining() const;

  LZ4F_compressionContext_t ctx_ = {};
  void* buf_ = nullptr;
  size_t buf_max_ = 0;
  size_t buf_used_ = 0;
};

class LZ4Decompressor : public Decompressor {
 public:
  LZ4Decompressor() = default;
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(LZ4Decompressor);

  // Decompressor implementation.
  virtual zx_status_t Decompress(void* uncompressed_buf, size_t* uncompressed_size,
                                 const void* compressed_buf,
                                 const size_t max_compressed_size) final;
};

// Decompress the source buffer into the target buffer, until either the source is drained or
// the target is filled (or both).
zx_status_t LZ4Decompress(void* target_buf, size_t* target_size, const void* src_buf,
                          size_t* src_size);

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_COMPRESSION_LZ4_H_
