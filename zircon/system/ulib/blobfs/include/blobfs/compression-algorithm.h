// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BLOBFS_COMPRESSION_ALGORITHM_H_
#define BLOBFS_COMPRESSION_ALGORITHM_H_

#include <stdint.h>

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
CompressionAlgorithm AlgorithmForInode(const Inode& inode);

// Return an Inode header flagset with the flags associated with |algorithm|
// set, and all other flags are unset.
uint16_t CompressionInodeHeaderFlags(const CompressionAlgorithm& algorithm);

}  // namespace blobfs

#endif  // BLOBFS_COMPRESSION_ALGORITHM_H_
