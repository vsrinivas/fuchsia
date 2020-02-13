// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_ALGORITHM_H_
#define ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_ALGORITHM_H_

namespace blobfs {

// Unique identifiers for each `Compressor`/`Decompressor` strategy.
enum class CompressionAlgorithm {
  LZ4,
  ZSTD,
  ZSTD_SEEKABLE,
};

}  // namespace blobfs

#endif  // ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_ALGORITHM_H_
