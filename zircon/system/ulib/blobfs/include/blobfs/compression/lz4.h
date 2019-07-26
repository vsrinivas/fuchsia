// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <blobfs/compression/compressor.h>
#include <fbl/unique_ptr.h>
#include <lz4/lz4frame.h>
#include <zircon/types.h>

namespace blobfs {

class LZ4Compressor : public Compressor {
 public:
  // Returns the maximum possible size a buffer would need to be
  // in order to compress data of size |input_length|.
  static size_t BufferMax(size_t input_length);

  static zx_status_t Create(size_t input_size, void* compression_buffer,
                            size_t compression_buffer_length, fbl::unique_ptr<LZ4Compressor>* out);
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

// Decompress the source buffer into the target buffer, until either the source is drained or
// the target is filled (or both).
zx_status_t LZ4Decompress(void* target_buf, size_t* target_size, const void* src_buf,
                          size_t* src_size);

}  // namespace blobfs
