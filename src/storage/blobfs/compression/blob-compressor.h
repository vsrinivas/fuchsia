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

#include <blobfs/compression-settings.h>
#include <fbl/macros.h>
#include <lz4/lz4frame.h>
#include <zstd/zstd.h>

#include "compressor.h"

namespace blobfs {

// A BlobCompressor is used to compress a blob transparently before it is written
// back to disk. This object owns the compression buffer, and abstracts away the
// differences between compression algorithms.
class BlobCompressor {
 public:
  // Initializes a compression object given the requested |settings| and input |blob_size|.
  static std::optional<BlobCompressor> Create(CompressionSettings settings, size_t blob_size);

  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(BlobCompressor);
  ~BlobCompressor();

  BlobCompressor(BlobCompressor&& o)
      : compressor_(std::move(o.compressor_)), compressed_blob_(std::move(o.compressed_blob_)) {}

  BlobCompressor& operator=(BlobCompressor&& o) {
    compressor_ = std::move(o.compressor_);
    compressed_blob_ = std::move(o.compressed_blob_);
    return *this;
  }

  size_t Size() const { return compressor_->Size(); }

  zx_status_t Update(const void* input_data, size_t input_length) {
    return compressor_->Update(input_data, input_length);
  }

  zx_status_t End() { return compressor_->End(); }

  // Returns a reference to a VMO containing the compressed blob.
  const zx::vmo& Vmo() const { return compressed_blob_.vmo(); }
  // Returns a reference to the compression buffer.
  const void* Data() const { return compressed_blob_.start(); }

 private:
  BlobCompressor(std::unique_ptr<Compressor> compressor, fzl::OwnedVmoMapper compressed_blob);

  std::unique_ptr<Compressor> compressor_;
  fzl::OwnedVmoMapper compressed_blob_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_COMPRESSION_BLOB_COMPRESSOR_H_
