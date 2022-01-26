// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_CHUNKED_COMPRESSION_CHUNKED_COMPRESSOR_H_
#define SRC_LIB_CHUNKED_COMPRESSION_CHUNKED_COMPRESSOR_H_

#include <lib/fit/function.h>

#include <fbl/array.h>
#include <fbl/macros.h>

#include "chunked-archive.h"
#include "compression-params.h"
#include "status.h"
#include "streaming-chunked-compressor.h"

namespace chunked_compression {

// ChunkedCompressor creates compressed archives by compressing an input buffer.
//
// Usage (error checks omitted):
//
//   const void* input = Input();
//   size_t input_len = InputDataSize();
//
//   ChunkedCompressor compressor;
//   size_t output_limit = compressor.ComputeOutputSizeLimit(input_len);
//
//   fbl::Array<uint8_t> output(new uint8_t[output_limit], output_limit);
//
//   size_t bytes_written;
//   compressor.Compress(input, input_len, output.get(), output.size(), &bytes_written);
class ChunkedCompressor {
 public:
  ChunkedCompressor();
  explicit ChunkedCompressor(CompressionParams params);
  ~ChunkedCompressor();
  ChunkedCompressor(ChunkedCompressor&& o) = default;
  ChunkedCompressor& operator=(ChunkedCompressor&& o) = default;
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ChunkedCompressor);

  // Convenience method to do a one-shot compression of |input|, returning an allocated
  // buffer containing the compressed bytes.
  static Status CompressBytes(const void* input, size_t input_len, fbl::Array<uint8_t>* output,
                              size_t* bytes_written_out);

  // Returns the minimum size that a buffer must be to hold the result of compressing |len| bytes.
  size_t ComputeOutputSizeLimit(size_t len) { return inner_.ComputeOutputSizeLimit(len); }

  // Reads from |input| and writes the compressed representation to |output|.
  // |output_len| must be at least |ComputeOutputSizeLimit(input_len)| bytes long.
  // Returns the number of compressed bytes written in |bytes_written_out|.
  Status Compress(const void* input, size_t input_len, void* output, size_t output_len,
                  size_t* bytes_written_out);

  // Registers |callback| to be invoked after each frame is complete.
  using ProgressFn =
      fit::function<void(size_t bytes_read, size_t bytes_total, size_t bytes_written)>;
  void SetProgressCallback(ProgressFn callback) { inner_.SetProgressCallback(std::move(callback)); }

 private:
  StreamingChunkedCompressor inner_;
};

}  // namespace chunked_compression

#endif  // SRC_LIB_CHUNKED_COMPRESSION_CHUNKED_COMPRESSOR_H_
