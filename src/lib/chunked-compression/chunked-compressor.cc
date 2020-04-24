// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include <algorithm>

#include <src/lib/chunked-compression/chunked-compressor.h>
#include <src/lib/chunked-compression/status.h>
#include <src/lib/chunked-compression/streaming-chunked-compressor.h>

namespace chunked_compression {

ChunkedCompressor::ChunkedCompressor() : ChunkedCompressor(CompressionParams{}) {}

ChunkedCompressor::ChunkedCompressor(CompressionParams params) : inner_(params) {}

ChunkedCompressor::~ChunkedCompressor() {}

Status ChunkedCompressor::CompressBytes(const void* input, size_t input_len,
                                        fbl::Array<uint8_t>* output, size_t* bytes_written_out) {
  CompressionParams params;
  ChunkedCompressor compressor(params);
  size_t output_len = params.ComputeOutputSizeLimit(input_len);
  fbl::Array<uint8_t> buf(new uint8_t[output_len], output_len);
  Status status = compressor.Compress(input, input_len, buf.get(), output_len, bytes_written_out);
  if (status == kStatusOk) {
    *output = std::move(buf);
  }
  return status;
}

Status ChunkedCompressor::Compress(const void* input, size_t input_len, void* output,
                                   size_t output_len, size_t* bytes_written_out) {
  if (input_len == 0) {
    *bytes_written_out = 0ul;
    return kStatusOk;
  }
  Status status = inner_.Init(input_len, output, output_len);
  if (status != kStatusOk) {
    return status;
  }
  status = inner_.Update(input, input_len);
  if (status != kStatusOk) {
    return status;
  }
  status = inner_.Final(bytes_written_out);
  if (status != kStatusOk) {
    return status;
  }
  return kStatusOk;
}

}  // namespace chunked_compression
