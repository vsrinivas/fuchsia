// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/metrics/compression_metrics.h"

namespace blobfs {

void CompressionMetrics::Update(const InodePtr& inode) {
  static_assert(kBlobFlagMaskAnyCompression == kBlobFlagChunkCompressed,
                "Need to update compression stats to handle multiple formats.");
  if (inode->header.IsCompressedZstdChunked()) {
    zstd_chunked_bytes_ += inode->blob_size;
  } else {
    uncompressed_bytes_ += inode->blob_size;
  }
}

CompressionMetrics::Properties CompressionMetrics::Attach(inspect::Node& node) const {
  return CompressionMetrics::Properties{
      .uncompressed_bytes = node.CreateUint("uncompressed_bytes", uncompressed_bytes_),
      .zstd_chunked_bytes = node.CreateUint("zstd_chunked_bytes", zstd_chunked_bytes_),
  };
}

}  // namespace blobfs
