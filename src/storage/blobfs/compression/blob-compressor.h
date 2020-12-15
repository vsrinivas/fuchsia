// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_COMPRESSION_BLOB_COMPRESSOR_H_
#define SRC_STORAGE_BLOBFS_COMPRESSION_BLOB_COMPRESSOR_H_

#ifndef __Fuchsia__
static_assert(false, "Fuchsia only header");
#endif

#include <lib/fzl/owned-vmo-mapper.h>
#include <zircon/types.h>

#include <memory>
#include <optional>

#include <fbl/macros.h>
#include <lz4/lz4frame.h>
#include <zstd/zstd.h>

#include "src/storage/blobfs/compression-settings.h"
#include "src/storage/blobfs/compression/compressor.h"

namespace blobfs {

// A BlobCompressor is used to compress a blob transparently before it is written
// back to disk. This object owns the compression buffer, and abstracts away the
// differences between compression algorithms.
class BlobCompressor {
 public:
  // Initializes a compression object given the requested |settings| and input |blob_size|.
  static std::optional<BlobCompressor> Create(CompressionSettings settings, size_t blob_size);

  BlobCompressor(BlobCompressor&& o) = default;
  BlobCompressor& operator=(BlobCompressor&& o) = default;

  size_t Size() const { return compressor_->Size(); }

  zx_status_t Update(const void* input_data, size_t input_length) {
    return compressor_->Update(input_data, input_length);
  }

  zx_status_t End() { return compressor_->End(); }

  // Returns a reference to a VMO containing the compressed blob.
  const zx::vmo& Vmo() const { return compressed_blob_.vmo(); }
  // Returns a reference to the compression buffer.
  const void* Data() const { return compressed_blob_.start(); }

  const Compressor& compressor() { return *compressor_; }
  CompressionAlgorithm algorithm() const { return algorithm_; }

 private:
  BlobCompressor(std::unique_ptr<Compressor> compressor, fzl::OwnedVmoMapper compressed_blob,
                 CompressionAlgorithm algorithm);

  std::unique_ptr<Compressor> compressor_;
  fzl::OwnedVmoMapper compressed_blob_;
  CompressionAlgorithm algorithm_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_COMPRESSION_BLOB_COMPRESSOR_H_
