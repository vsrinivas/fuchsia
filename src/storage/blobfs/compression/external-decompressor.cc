// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/compression/external-decompressor.h"

namespace blobfs {

CompressionAlgorithm CompressionAlgorithmFidlToLocal(
    const llcpp::fuchsia::blobfs::internal::CompressionAlgorithm algorithm) {
  using Fidl = llcpp::fuchsia::blobfs::internal::CompressionAlgorithm;
  switch(algorithm) {
    case Fidl::UNCOMPRESSED:
      return CompressionAlgorithm::UNCOMPRESSED;
    case Fidl::LZ4:
      return CompressionAlgorithm::LZ4;
    case Fidl::ZSTD:
      return CompressionAlgorithm::ZSTD;
    case Fidl::ZSTD_SEEKABLE:
      return CompressionAlgorithm::ZSTD_SEEKABLE;
    case Fidl::CHUNKED:
      return CompressionAlgorithm::CHUNKED;
  }
}

llcpp::fuchsia::blobfs::internal::CompressionAlgorithm CompressionAlgorithmLocalToFidl(
    CompressionAlgorithm algorithm) {
  using Fidl = llcpp::fuchsia::blobfs::internal::CompressionAlgorithm;
  switch(algorithm) {
    case CompressionAlgorithm::UNCOMPRESSED:
      return Fidl::UNCOMPRESSED;
    case CompressionAlgorithm::LZ4:
      return Fidl::LZ4;
    case CompressionAlgorithm::ZSTD:
      return Fidl::ZSTD;
    case CompressionAlgorithm::ZSTD_SEEKABLE:
      return Fidl::ZSTD_SEEKABLE;
    case CompressionAlgorithm::CHUNKED:
      return Fidl::CHUNKED;
  }
}

}  // namespace blobfs
