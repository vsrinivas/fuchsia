// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "algorithm.h"

#include <stdint.h>
#include <zircon/assert.h>

#include <optional>

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

std::optional<CompressionAlgorithm> AlgorithmFromInodeHeaderFlags(uint16_t flags) {
  if (flags & kBlobFlagLZ4Compressed) {
    return std::make_optional(CompressionAlgorithm::LZ4);
  } else if (flags & kBlobFlagZSTDCompressed) {
    return std::make_optional(CompressionAlgorithm::ZSTD);
  } else if (flags & kBlobFlagZSTDSeekableCompressed) {
    return std::make_optional(CompressionAlgorithm::ZSTD_SEEKABLE);
  } else {
    return {};
  }
}

}  // namespace blobfs
