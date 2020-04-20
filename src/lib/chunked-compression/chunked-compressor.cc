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

Status ChunkedCompressor::CompressBytes(const void* data, size_t data_len,
                                        fbl::Array<uint8_t>* compressed_data_out,
                                        size_t* bytes_written_out) {
  CompressionParams params;
  ChunkedCompressor compressor(params);
  size_t out_len = params.ComputeOutputSizeLimit(data_len);
  fbl::Array<uint8_t> buf(new uint8_t[out_len], out_len);
  Status status = compressor.Compress(data, data_len, buf.get(), out_len, bytes_written_out);
  if (status == kStatusOk) {
    *compressed_data_out = std::move(buf);
  }
  return status;
}

Status ChunkedCompressor::Compress(const void* data, size_t data_len, void* dst, size_t dst_len,
                                   size_t* bytes_written_out) {
  if (data_len == 0) {
    *bytes_written_out = 0ul;
    return kStatusOk;
  }
  Status status = inner_.Init(data_len, dst, dst_len);
  if (status != kStatusOk) {
    return status;
  }
  status = inner_.Update(data, data_len);
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
