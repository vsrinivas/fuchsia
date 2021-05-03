// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/compression_settings.h"

#include <lib/zx/status.h>
#include <stdint.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <src/lib/chunked-compression/compression-params.h>
#include <zstd/zstd.h>

#include "src/storage/blobfs/format.h"

namespace blobfs {

const char* CompressionAlgorithmToString(CompressionAlgorithm algorithm) {
  switch (algorithm) {
    case CompressionAlgorithm::kLz4:
      return "LZ4";
    case CompressionAlgorithm::kZstd:
      return "ZSTD";
    case CompressionAlgorithm::kZstdSeekable:
      return "ZSTD_SEEKABLE";
    case CompressionAlgorithm::kChunked:
      return "ZSTD_CHUNKED";
    case CompressionAlgorithm::kUncompressed:
      return "UNCOMPRESSED";
  }
}

zx::status<CompressionAlgorithm> AlgorithmForInode(const Inode& inode) {
  static_assert(
      kBlobFlagMaskAnyCompression == (kBlobFlagLZ4Compressed | kBlobFlagZSTDCompressed |
                                      kBlobFlagChunkCompressed | kBlobFlagZSTDSeekableCompressed),
      "Missing algorithm case");

  switch (inode.header.flags & kBlobFlagMaskAnyCompression) {
    case 0:
      return zx::ok(CompressionAlgorithm::kUncompressed);
    case kBlobFlagLZ4Compressed:
      return zx::ok(CompressionAlgorithm::kLz4);
    case kBlobFlagZSTDCompressed:
      return zx::ok(CompressionAlgorithm::kZstd);
    case kBlobFlagZSTDSeekableCompressed:
      return zx::ok(CompressionAlgorithm::kZstdSeekable);
    case kBlobFlagChunkCompressed:
      return zx::ok(CompressionAlgorithm::kChunked);
    default:
      // Multiple flags are set, or these conversion methods have not been updated to be in sync
      // with kBlobFlagMaskAnyCompression.
      return zx::error(ZX_ERR_INVALID_ARGS);
  }
}

uint16_t CompressionInodeHeaderFlags(const CompressionAlgorithm& algorithm) {
  switch (algorithm) {
    case CompressionAlgorithm::kUncompressed:
      return 0;
    case CompressionAlgorithm::kLz4:
      return kBlobFlagLZ4Compressed;
    case CompressionAlgorithm::kZstd:
      return kBlobFlagZSTDCompressed;
    case CompressionAlgorithm::kZstdSeekable:
      return kBlobFlagZSTDSeekableCompressed;
    case CompressionAlgorithm::kChunked:
      return kBlobFlagChunkCompressed;
  }
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
    case CompressionAlgorithm::kLz4:
    case CompressionAlgorithm::kUncompressed:
      // compression_level shouldn't be set in these cases.
      return false;
    case CompressionAlgorithm::kZstd:
    case CompressionAlgorithm::kZstdSeekable:
      return *compression_level >= ZSTD_minCLevel() && *compression_level <= ZSTD_maxCLevel();
    case CompressionAlgorithm::kChunked:
      return *compression_level >= chunked_compression::CompressionParams::MinCompressionLevel() &&
             *compression_level <= chunked_compression::CompressionParams::MaxCompressionLevel();
  }
}

}  // namespace blobfs
