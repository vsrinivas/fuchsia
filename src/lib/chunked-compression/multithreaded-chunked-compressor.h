// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_CHUNKED_COMPRESSION_MULTITHREADED_CHUNKED_COMPRESSOR_H_
#define SRC_LIB_CHUNKED_COMPRESSION_MULTITHREADED_CHUNKED_COMPRESSOR_H_

#include <lib/stdcompat/span.h>
#include <lib/zx/status.h>

#include <cstdint>
#include <memory>

#include "compression-params.h"

namespace chunked_compression {

// MultithreadedChunkedCompressor creates compressed archives by using a thread pool to compress
// chunks in parallel. This class is thread safe and can be used to compress multiple buffers at the
// same time.
class MultithreadedChunkedCompressor {
 public:
  explicit MultithreadedChunkedCompressor(size_t thread_count);
  ~MultithreadedChunkedCompressor();

  // Compresses |input| and returns the compressed result.
  zx::result<std::vector<uint8_t>> Compress(const CompressionParams& params,
                                            cpp20::span<const uint8_t> input);

 private:
  class MultithreadedChunkedCompressorImpl;
  std::unique_ptr<MultithreadedChunkedCompressorImpl> impl_;
};

}  // namespace chunked_compression

#endif  // SRC_LIB_CHUNKED_COMPRESSION_MULTITHREADED_CHUNKED_COMPRESSOR_H_
