// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_INCLUDE_BLOBFS_COMPRESSION_SETTINGS_H_
#define SRC_STORAGE_BLOBFS_INCLUDE_BLOBFS_COMPRESSION_SETTINGS_H_

#include <lib/zx/status.h>
#include <stdint.h>

#include <optional>

#include <blobfs/format.h>

namespace blobfs {

// Unique identifiers for each |Compressor|/|Decompressor| strategy.
enum class CompressionAlgorithm {
  UNCOMPRESSED = 0,
  LZ4,
  ZSTD,
  ZSTD_SEEKABLE,
  CHUNKED,
};

const char* CompressionAlgorithmToString(CompressionAlgorithm);

// Returns the compression algorithm used in |inode|.
zx::status<CompressionAlgorithm> AlgorithmForInode(const Inode& inode);

// Return an Inode header flagset with the flags associated with |algorithm|
// set, and all other flags are unset.
uint16_t CompressionInodeHeaderFlags(const CompressionAlgorithm& algorithm);

// Clear any existing compression flags and apply the new one.
void SetCompressionAlgorithm(Inode* inode, const CompressionAlgorithm algorithm);

// Settings to configure compression behavior.
struct CompressionSettings {
  // Compression algorithm to use when storing blobs.
  // Blobs that are already stored on disk using another compression algorithm from disk are not
  // affected by this flag.
  CompressionAlgorithm compression_algorithm = CompressionAlgorithm::CHUNKED;
  // Write compression aggressiveness. Currently only used for ZSTD* and CHUNKED algorithms.
  // If set to std::nullopt, an implementation-defined default is used.
  std::optional<int> compression_level;

  // Returns true if the configured settings are valid.
  bool IsValid() const;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_INCLUDE_BLOBFS_COMPRESSION_SETTINGS_H_
