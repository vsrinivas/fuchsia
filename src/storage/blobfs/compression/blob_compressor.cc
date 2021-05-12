// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/compression/blob_compressor.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <memory>

#include <fbl/algorithm.h>
#include <fbl/macros.h>

#include "src/storage/blobfs/compression/chunked.h"

namespace blobfs {

std::optional<BlobCompressor> BlobCompressor::Create(CompressionSettings settings,
                                                     size_t uncompressed_blob_size) {
  switch (settings.compression_algorithm) {
    case CompressionAlgorithm::kChunked: {
      std::unique_ptr<ChunkedCompressor> compressor;
      size_t max;
      zx_status_t status =
          ChunkedCompressor::Create(settings, uncompressed_blob_size, &max, &compressor);
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to create compressor: " << zx_status_get_string(status);
        return std::nullopt;
      }
      fzl::OwnedVmoMapper compressed_inmemory_blob;
      max = fbl::round_up(max, kBlobfsBlockSize);
      status = compressed_inmemory_blob.CreateAndMap(max, "chunk-compressed-blob");
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to create mapping for compressed data: "
                       << zx_status_get_string(status);
        return std::nullopt;
      }
      status =
          compressor->SetOutput(compressed_inmemory_blob.start(), compressed_inmemory_blob.size());
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to initialize compressor: " << zx_status_get_string(status);
        return std::nullopt;
      }
      return BlobCompressor(std::move(compressor), std::move(compressed_inmemory_blob),
                            settings.compression_algorithm);
    }
    case CompressionAlgorithm::kUncompressed:
      ZX_DEBUG_ASSERT(false);
      return std::nullopt;
  }

  ZX_DEBUG_ASSERT(false);
  return std::nullopt;
}

BlobCompressor::BlobCompressor(std::unique_ptr<Compressor> compressor,
                               fzl::OwnedVmoMapper compressed_buffer,
                               CompressionAlgorithm algorithm)
    : compressor_(std::move(compressor)),
      compressed_buffer_(std::move(compressed_buffer)),
      algorithm_(algorithm) {
  ZX_ASSERT(algorithm_ != CompressionAlgorithm::kUncompressed);
}

}  // namespace blobfs
