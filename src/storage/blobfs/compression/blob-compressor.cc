// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/compression/blob-compressor.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <memory>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/macros.h>

#include "src/storage/blobfs/compression/chunked.h"
#include "src/storage/blobfs/compression/lz4.h"
#include "src/storage/blobfs/compression/zstd-plain.h"
#include "src/storage/blobfs/compression/zstd-seekable.h"

namespace blobfs {

std::optional<BlobCompressor> BlobCompressor::Create(CompressionSettings settings,
                                                     size_t blob_size) {
  switch (settings.compression_algorithm) {
    case CompressionAlgorithm::LZ4: {
      fzl::OwnedVmoMapper compressed_blob;
      const size_t max = fbl::round_up(LZ4Compressor::BufferMax(blob_size), kBlobfsBlockSize);
      zx_status_t status = compressed_blob.CreateAndMap(max, "lz4-blob");
      if (status != ZX_OK) {
        return std::nullopt;
      }
      std::unique_ptr<LZ4Compressor> compressor;
      status = LZ4Compressor::Create(blob_size, compressed_blob.start(), compressed_blob.size(),
                                     &compressor);
      if (status != ZX_OK) {
        return std::nullopt;
      }
      auto result = BlobCompressor(std::move(compressor), std::move(compressed_blob),
                                   settings.compression_algorithm);
      return std::make_optional(std::move(result));
    }
    case CompressionAlgorithm::ZSTD: {
      fzl::OwnedVmoMapper compressed_blob;
      const size_t max = fbl::round_up(ZSTDCompressor::BufferMax(blob_size), kBlobfsBlockSize);
      zx_status_t status = compressed_blob.CreateAndMap(max, "zstd-blob");
      if (status != ZX_OK) {
        return std::nullopt;
      }
      std::unique_ptr<ZSTDCompressor> compressor;
      status = ZSTDCompressor::Create(settings, blob_size, compressed_blob.start(),
                                      compressed_blob.size(), &compressor);
      if (status != ZX_OK) {
        return std::nullopt;
      }
      auto result = BlobCompressor(std::move(compressor), std::move(compressed_blob),
                                   settings.compression_algorithm);
      return std::make_optional(std::move(result));
    }
    case CompressionAlgorithm::ZSTD_SEEKABLE: {
      fzl::OwnedVmoMapper compressed_blob;
      const size_t max =
          fbl::round_up(ZSTDSeekableCompressor::BufferMax(blob_size), kBlobfsBlockSize);
      zx_status_t status = compressed_blob.CreateAndMap(max, "zstd-seekable-blob");
      if (status != ZX_OK) {
        return std::nullopt;
      }
      std::unique_ptr<ZSTDSeekableCompressor> compressor;
      status = ZSTDSeekableCompressor::Create(settings, blob_size, compressed_blob.start(),
                                              compressed_blob.size(), &compressor);
      if (status != ZX_OK) {
        return std::nullopt;
      }
      auto result = BlobCompressor(std::move(compressor), std::move(compressed_blob),
                                   settings.compression_algorithm);
      return std::make_optional(std::move(result));
    }
    case CompressionAlgorithm::CHUNKED: {
      std::unique_ptr<ChunkedCompressor> compressor;
      size_t max;
      zx_status_t status = ChunkedCompressor::Create(settings, blob_size, &max, &compressor);
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to create compressor: " << zx_status_get_string(status);
        return std::nullopt;
      }
      fzl::OwnedVmoMapper compressed_blob;
      max = fbl::round_up(max, kBlobfsBlockSize);
      status = compressed_blob.CreateAndMap(max, "chunk-compressed-blob");
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to create mapping for compressed data: "
                       << zx_status_get_string(status);
        return std::nullopt;
      }
      status = compressor->SetOutput(compressed_blob.start(), compressed_blob.size());
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to initialize compressor: "
                       << zx_status_get_string(status);
        return std::nullopt;
      }
      return BlobCompressor(std::move(compressor), std::move(compressed_blob),
                            settings.compression_algorithm);
    }
    case CompressionAlgorithm::UNCOMPRESSED:
      ZX_DEBUG_ASSERT(false);
      return std::nullopt;
  }
}

BlobCompressor::BlobCompressor(std::unique_ptr<Compressor> compressor,
                               fzl::OwnedVmoMapper compressed_blob, CompressionAlgorithm algorithm)
    : compressor_(std::move(compressor)),
      compressed_blob_(std::move(compressed_blob)),
      algorithm_(algorithm) {
  ZX_ASSERT(algorithm_ != CompressionAlgorithm::UNCOMPRESSED);
}

}  // namespace blobfs
