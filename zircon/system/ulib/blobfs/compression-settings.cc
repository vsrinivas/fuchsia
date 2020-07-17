// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <lib/zx/status.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <blobfs/compression-settings.h>
#include <blobfs/format.h>
#include <src/lib/chunked-compression/compression-params.h>
#include <zstd/zstd.h>

namespace blobfs {

const char* CompressionAlgorithmToString(CompressionAlgorithm algorithm) {
  switch (algorithm) {
    case CompressionAlgorithm::LZ4:
      return "LZ4";
    case CompressionAlgorithm::ZSTD:
      return "ZSTD";
    case CompressionAlgorithm::ZSTD_SEEKABLE:
      return "ZSTD_SEEKABLE";
    case CompressionAlgorithm::CHUNKED:
      return "ZSTD_CHUNKED";
    case CompressionAlgorithm::UNCOMPRESSED:
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
      return zx::ok(CompressionAlgorithm::UNCOMPRESSED);
    case kBlobFlagLZ4Compressed:
      return zx::ok(CompressionAlgorithm::LZ4);
    case kBlobFlagZSTDCompressed:
      return zx::ok(CompressionAlgorithm::ZSTD);
    case kBlobFlagZSTDSeekableCompressed:
      return zx::ok(CompressionAlgorithm::ZSTD_SEEKABLE);
    case kBlobFlagChunkCompressed:
      return zx::ok(CompressionAlgorithm::CHUNKED);
    default:
      // Multiple flags are set, or these conversion methods have not been
      // updated to be in sync with kBlobFlagMaskAnyCompression.
      return zx::error(ZX_ERR_INVALID_ARGS);
  }
}

uint16_t CompressionInodeHeaderFlags(const CompressionAlgorithm& algorithm) {
  switch (algorithm) {
    case CompressionAlgorithm::UNCOMPRESSED:
      return 0;
    case CompressionAlgorithm::LZ4:
      return kBlobFlagLZ4Compressed;
    case CompressionAlgorithm::ZSTD:
      return kBlobFlagZSTDCompressed;
    case CompressionAlgorithm::ZSTD_SEEKABLE:
      return kBlobFlagZSTDSeekableCompressed;
    case CompressionAlgorithm::CHUNKED:
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
    case CompressionAlgorithm::LZ4:
    case CompressionAlgorithm::UNCOMPRESSED:
      // compression_level shouldn't be set in these cases.
      return false;
    case CompressionAlgorithm::ZSTD:
    case CompressionAlgorithm::ZSTD_SEEKABLE:
      return *compression_level >= ZSTD_minCLevel() && *compression_level <= ZSTD_maxCLevel();
    case CompressionAlgorithm::CHUNKED:
      return *compression_level >= chunked_compression::CompressionParams::MinCompressionLevel() &&
             *compression_level <= chunked_compression::CompressionParams::MaxCompressionLevel();
  }
}

}  // namespace blobfs
