// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/compression_settings.h"

#include <lib/zx/result.h>
#include <stdint.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <src/lib/chunked-compression/compression-params.h>

#include "src/storage/blobfs/format.h"

namespace blobfs {

const char* CompressionAlgorithmToString(CompressionAlgorithm algorithm) {
  switch (algorithm) {
    case CompressionAlgorithm::kChunked:
      return "ZSTD_CHUNKED";
    case CompressionAlgorithm::kUncompressed:
      return "UNCOMPRESSED";
  }

  ZX_DEBUG_ASSERT(false);
  return "INVALID";
}

zx::result<CompressionAlgorithm> AlgorithmForInode(const Inode& inode) {
  static_assert(kBlobFlagMaskAnyCompression == kBlobFlagChunkCompressed, "Missing algorithm case");

  switch (inode.header.flags & kBlobFlagMaskAnyCompression) {
    case 0:
      return zx::ok(CompressionAlgorithm::kUncompressed);
    case kBlobFlagChunkCompressed:
      return zx::ok(CompressionAlgorithm::kChunked);
    default:
      // Conversion methods have not been updated to be in sync with kBlobFlagMaskAnyCompression.
      return zx::error(ZX_ERR_INVALID_ARGS);
  }
}

uint16_t CompressionInodeHeaderFlags(const CompressionAlgorithm& algorithm) {
  switch (algorithm) {
    case CompressionAlgorithm::kUncompressed:
      return 0;
    case CompressionAlgorithm::kChunked:
      return kBlobFlagChunkCompressed;
  }

  ZX_DEBUG_ASSERT(false);
  return 0;
}

void SetCompressionAlgorithm(Inode* inode, const CompressionAlgorithm algorithm) {
  uint16_t* flags = &(inode->header.flags);
  *flags &= ~kBlobFlagMaskAnyCompression;
  *flags |= CompressionInodeHeaderFlags(algorithm);
}

bool CompressionSettings::IsValid() const {
  if (compression_level == std::nullopt) {
    return true;
  }
  switch (compression_algorithm) {
    case CompressionAlgorithm::kUncompressed:
      // compression_level shouldn't be set in these cases.
      return false;
    case CompressionAlgorithm::kChunked:
      return *compression_level >= chunked_compression::CompressionParams::MinCompressionLevel() &&
             *compression_level <= chunked_compression::CompressionParams::MaxCompressionLevel();
  }

  return false;
}

}  // namespace blobfs
