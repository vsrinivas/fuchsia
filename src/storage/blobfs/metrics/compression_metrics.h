// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_METRICS_COMPRESSION_METRICS_H_
#define SRC_STORAGE_BLOBFS_METRICS_COMPRESSION_METRICS_H_

#include <lib/inspect/cpp/inspect.h>

#include "src/storage/blobfs/node_finder.h"

namespace blobfs {

// Encapsulates Blobfs compression metrics. **NOT thread-safe**.
struct CompressionMetrics {
  // Inspect properties representing the compression metrics.
  struct Properties {
    inspect::UintProperty uncompressed_bytes;
    inspect::UintProperty zstd_chunked_bytes;
  };

  CompressionMetrics() = default;

  // Update compression metrics with stats from the given |inode|.
  void Update(const InodePtr& inode);

  // Attach the current values of compression metrics to the given |node|, returning ownership
  // of the newly created Inspect properties.
  Properties Attach(inspect::Node& node) const;

 private:
  uint64_t uncompressed_bytes_ = {};
  uint64_t zstd_chunked_bytes_ = {};
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_METRICS_COMPRESSION_METRICS_H_
