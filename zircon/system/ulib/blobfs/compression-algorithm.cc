// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include <stdint.h>
#include <zircon/assert.h>

#include <blobfs/compression-algorithm.h>
#include <blobfs/format.h>

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
    default:
      ZX_ASSERT(false);
      return "";
  }
}

CompressionAlgorithm AlgorithmForInode(const Inode& inode) {
  if (inode.header.flags & kBlobFlagLZ4Compressed) {
    return CompressionAlgorithm::LZ4;
  } else if (inode.header.flags & kBlobFlagZSTDCompressed) {
    return CompressionAlgorithm::ZSTD;
  } else if (inode.header.flags & kBlobFlagZSTDSeekableCompressed) {
    return CompressionAlgorithm::ZSTD_SEEKABLE;
  } else if (inode.header.flags & kBlobFlagChunkCompressed) {
    return CompressionAlgorithm::CHUNKED;
  }
  return CompressionAlgorithm::UNCOMPRESSED;
}

uint16_t CompressionInodeHeaderFlags(const CompressionAlgorithm& algorithm) {
  switch (algorithm) {
    case CompressionAlgorithm::LZ4:
      return kBlobFlagLZ4Compressed;
    case CompressionAlgorithm::ZSTD:
      return kBlobFlagZSTDCompressed;
    case CompressionAlgorithm::ZSTD_SEEKABLE:
      return kBlobFlagZSTDSeekableCompressed;
    case CompressionAlgorithm::CHUNKED:
      return kBlobFlagChunkCompressed;
    default:
      ZX_ASSERT(false);
      return kBlobFlagZSTDCompressed;
  }
}

}  // namespace blobfs
