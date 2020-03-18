// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "algorithm.h"

#include <stdint.h>
#include <zircon/assert.h>

#include <blobfs/format.h>

namespace blobfs {

uint16_t CompressionInodeHeaderFlags(const CompressionAlgorithm& algorithm) {
  switch (algorithm) {
    case CompressionAlgorithm::LZ4:
      return kBlobFlagLZ4Compressed;
    case CompressionAlgorithm::ZSTD:
      return kBlobFlagZSTDCompressed;
    case CompressionAlgorithm::ZSTD_SEEKABLE:
      return kBlobFlagZSTDSeekableCompressed;
    default:
      ZX_ASSERT(false);
      return kBlobFlagZSTDCompressed;
  }
}

}  // namespace blobfs
