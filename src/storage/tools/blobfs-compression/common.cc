// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "blobfs-compression.h"
#include "src/lib/chunked-compression/chunked-compressor.h"
#include "src/lib/chunked-compression/status.h"

namespace blobfs_compress {
namespace {
using chunked_compression::ChunkedCompressor;
using chunked_compression::CompressionParams;
using chunked_compression::ToZxStatus;

constexpr const char kAnsiUpLine[] = "\33[A";
constexpr const char kAnsiClearLine[] = "\33[2K\r";

// TODO (fxbug.dev/66779): Use blobfs compression level directly instead of hardcoding.
constexpr int kDefaultBlobfsCompressionLevel = 14;
constexpr size_t kTargetFrameSize = 32 * 1024;
}  // namespace

// ProgressWriter writes live a progress indicator to stdout. Updates are written in-place
// (using ANSI control codes to rewrite the current line).
class ProgressWriter {
 public:
  explicit ProgressWriter(int refresh_hz = 60) : refresh_hz_(refresh_hz) {
    last_report_ = std::chrono::steady_clock::time_point::min();
    printf("\n");
  }

  void Update(const char* fmt, ...) {
    auto now = std::chrono::steady_clock::now();
    if (now < last_report_ + refresh_duration()) {
      return;
    }
    last_report_ = now;
    printf("%s%s", kAnsiUpLine, kAnsiClearLine);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    fflush(stdout);
  }

  void Final(const char* fmt, ...) {
    printf("%s%s", kAnsiUpLine, kAnsiClearLine);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    fflush(stdout);
  }

  std::chrono::steady_clock::duration refresh_duration() const {
    return std::chrono::seconds(1) / refresh_hz_;
  }

 private:
  std::chrono::steady_clock::time_point last_report_;
  int refresh_hz_;
};

// Returns the exact same Blobfs compression parameters used in Fuchsia.
CompressionParams ComputeDefaultBlobfsCompressionParams(size_t sz) {
  CompressionParams params;

  // Use default param values, which are opaque to sdk users.
  // This allows us to fine tune these and keep them in sync with blobfs chunked
  // compression algorithm.
  params.frame_checksum = false;
  params.compression_level = kDefaultBlobfsCompressionLevel;
  params.chunk_size =
      CompressionParams::ChunkSizeForInputSize(sz, kTargetFrameSize);
  return params;
}

// Returns 0 if the compression runs successfully; otherwise non-zero values.
// This method reads |src_sz| from |src|, compresses it using the compression
// |params|, and then writes the compressed bytes to |dest_write_buf| and the
// compressed size to |out_compressed_size|.
//
// |dest_write_buf| can be nullptr if wanting the final compressed size only.
// However, even if |dest_write_buf| is set to nullptr, there will still be
// temporary RAM consumption for storing compressed data due to current internal
// compression API design.
zx_status_t BlobfsCompress(const uint8_t* src,
                   const size_t src_sz,
                   uint8_t* dest_write_buf,
                   size_t* out_compressed_size,
                   CompressionParams params) {
  ChunkedCompressor compressor(params);

  ProgressWriter progress;
  compressor.SetProgressCallback([&](size_t bytes_read,
                                     size_t bytes_total,
                                     size_t bytes_written) {
    progress.Update("%2.0f%% (%lu bytes written)\n",
                    static_cast<double>(bytes_read)
                        / static_cast<double>(bytes_total) * 100,
                    bytes_written);
  });

  size_t compressed_size;
  size_t output_limit = params.ComputeOutputSizeLimit(src_sz);
  std::vector<uint8_t> output_buffer;

  // The caller does not need the compressed data. However, the compressor
  // still requires a write buffer to store the compressed output.
  if (dest_write_buf == nullptr) {
    output_buffer.resize(output_limit);
    dest_write_buf = output_buffer.data();
  }

  const auto compression_status = compressor.Compress(src,
                                                      src_sz,
                                                      dest_write_buf,
                                                      output_limit,
                                                      &compressed_size);
  if (compression_status != chunked_compression::kStatusOk) {
    return ToZxStatus(compression_status);
  }

  double saving_ratio = static_cast<double>(src_sz - compressed_size);
  if (src_sz) {
    saving_ratio /= static_cast<double>(src_sz);
  } else {
    saving_ratio = 0;
  }
  progress.Final("Wrote %lu bytes (%.2f%% space saved).\n",
                 compressed_size,
                 saving_ratio * 100);

  *out_compressed_size = compressed_size;
  return ZX_OK;
}
}  // namespace blobfs_compress
