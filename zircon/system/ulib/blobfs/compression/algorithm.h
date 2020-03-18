// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_ALGORITHM_H_
#define ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_ALGORITHM_H_

#include <stdint.h>

namespace blobfs {

// Unique identifiers for each |Compressor|/|Decompressor| strategy.
enum class CompressionAlgorithm {
  LZ4,
  ZSTD,
  ZSTD_SEEKABLE,
};

// Return an Inode header flagset with the flags associated with |algorithm|
// set, and all other flags are unset.
uint16_t CompressionInodeHeaderFlags(const CompressionAlgorithm& algorithm);

}  // namespace blobfs

#endif  // ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_ALGORITHM_H_
