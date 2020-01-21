// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_ZSTD_RAC_H_
#define ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_ZSTD_RAC_H_

#include <zircon/types.h>

#include <memory>

#include <zstd/zstd.h>
#include <zstd/zstd_seekable.h>

#include "compressor.h"

namespace blobfs {

// Compressor implementation for the zstd seekable format library implemented in
// //third_party/zstd/contrib/seekable_format. The library provides a convenient API for
// random access in zstd archives.
class ZSTDSeekableCompressor : public Compressor {
 public:
  // Returns an upper bound on the size of the buffer required to store the compressed
  // representation of a blob of size `input_length`.
  static size_t BufferMax(size_t input_length);

  static zx_status_t Create(size_t input_size, void* compression_buffer,
                            size_t compression_buffer_length,
                            std::unique_ptr<ZSTDSeekableCompressor>* out);
  ~ZSTDSeekableCompressor();

  ////////////////////////////////////////
  // Compressor interface
  size_t Size() const final;
  zx_status_t Update(const void* input_data, size_t input_length) final;
  zx_status_t End() final;

 private:
  ZSTDSeekableCompressor(ZSTD_seekable_CStream* stream, void* compression_buffer,
                         size_t compression_buffer_length);

  ZSTD_seekable_CStream* stream_ = nullptr;
  ZSTD_outBuffer output_ = {};

  DISALLOW_COPY_ASSIGN_AND_MOVE(ZSTDSeekableCompressor);
};

// Decompress the source buffer into the target buffer, until either the source is drained or
// the target is filled (or both).
zx_status_t ZSTDSeekableDecompress(void* target_buf, size_t* target_size, const void* src_buf);

}  // namespace blobfs

#endif  // ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_ZSTD_RAC_H_
