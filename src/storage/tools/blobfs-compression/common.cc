// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <sys/stat.h>

#include <vector>

#include "blobfs-compression.h"
#include "src/lib/chunked-compression/chunked-compressor.h"
#include "src/lib/chunked-compression/status.h"
#include "src/lib/digest/merkle-tree.h"
#include "src/storage/blobfs/format.h"

namespace blobfs_compress {
namespace {
using ::chunked_compression::ChunkedCompressor;
using ::chunked_compression::CompressionParams;
using ::chunked_compression::ToZxStatus;
}  // namespace

// Validate command line |options| used for compressing.
zx_status_t ValidateCliOptions(const CompressionCliOptionStruct& options) {
  if (options.source_file.empty()) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Check source file.
  if (!options.source_file_fd.is_valid()) {
    fprintf(stderr, "Failed to open '%s'.\n", options.source_file.c_str());
    return ZX_ERR_BAD_PATH;
  }
  {
    struct stat info;
    if (fstat(options.source_file_fd.get(), &info) < 0) {
      fprintf(stderr, "stat(%s) failed: %s\n", options.source_file.c_str(), strerror(errno));
      return ZX_ERR_BAD_STATE;
    }
    if (!S_ISREG(info.st_mode)) {
      fprintf(stderr, "%s is not a regular file\n", options.source_file.c_str());
      return ZX_ERR_NOT_FILE;
    }
  }

  // Check compressed output file (can be empty).
  if (!options.compressed_file.empty() && !options.compressed_file_fd.is_valid()) {
    fprintf(stderr, "Failed to open '%s': %s\n", options.compressed_file.c_str(), strerror(errno));
    return ZX_ERR_BAD_PATH;
  }

  return ZX_OK;
}

// Returns 0 if the compression runs successfully; otherwise non-zero values.
// This method reads |src_sz| from |src|, compresses it using the compression
// |params|, and then writes the compressed bytes to |dest_write_buf| and the
// compressed size to |out_compressed_size|. |cli_options| will be used to
// configure what information to include in the output.
//
// |dest_write_buf| can be nullptr if wanting the final compressed size only.
// However, even if |dest_write_buf| is set to nullptr, there will still be
// temporary RAM consumption for storing compressed data due to current internal
// compression API design.
zx_status_t BlobfsCompress(const uint8_t* src, const size_t src_sz, uint8_t* dest_write_buf,
                           size_t* out_compressed_size, CompressionParams params,
                           const CompressionCliOptionStruct& cli_options) {
  ChunkedCompressor compressor(params);

  // Using non-compact merkle tree size by default because it's bigger than compact merkle tree.
  const size_t merkle_tree_size =
      digest::CalculateMerkleTreeSize(src_sz, digest::kDefaultNodeSize, false);
  size_t compressed_size;
  size_t output_limit = params.ComputeOutputSizeLimit(src_sz);
  std::vector<uint8_t> output_buffer;

  // The caller does not need the compressed data. However, the compressor
  // still requires a write buffer to store the compressed output.
  if (dest_write_buf == nullptr) {
    output_buffer.resize(fbl::round_up(output_limit + merkle_tree_size, blobfs::kBlobfsBlockSize));
    dest_write_buf = output_buffer.data();
  }

  const auto compression_status =
      compressor.Compress(src, src_sz, dest_write_buf, output_limit, &compressed_size);
  if (compression_status != chunked_compression::kStatusOk) {
    return ToZxStatus(compression_status);
  }

  // Final size output should be aligned with block size unless disabled explicitly.
  size_t aligned_source_size = src_sz;
  size_t aligned_compressed_size = compressed_size + merkle_tree_size;
  if (!cli_options.disable_size_alignment) {
    aligned_source_size = fbl::round_up(aligned_source_size, blobfs::kBlobfsBlockSize);
    aligned_compressed_size = fbl::round_up(aligned_compressed_size, blobfs::kBlobfsBlockSize);
  }

  double saving_ratio =
      static_cast<double>(aligned_source_size) - static_cast<double>(aligned_compressed_size);
  if (aligned_source_size) {
    saving_ratio /= static_cast<double>(aligned_source_size);
  } else {
    saving_ratio = 0;
  }

  // The format of this message is depended on by //tools/size_checker/cmd/size_checker.go.
  printf("Wrote %lu bytes (%.2f%% space saved).\n", aligned_compressed_size, saving_ratio * 100);

  // By default, filling 0x00 at the end of compressed buffer to match |aligned_compressed_size|.
  *out_compressed_size = aligned_compressed_size;
  return ZX_OK;
}
}  // namespace blobfs_compress
