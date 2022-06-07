// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect/cpp/inspect.h>

#include <map>

#ifndef SRC_STORAGE_BLOBFS_METRICS_FRAGMENTATION_METRICS_H_
#define SRC_STORAGE_BLOBFS_METRICS_FRAGMENTATION_METRICS_H_

namespace blobfs {

class Blobfs;

// Encapsulates Blobfs fragmentation metrics. Thread-safe.
struct FragmentationMetrics {
  FragmentationMetrics() = default;
  explicit FragmentationMetrics(inspect::Node& node);

  // Total number of inodes in the filesystem.
  inspect::UintProperty total_nodes;
  // Total number of files (blobs) in use.
  inspect::UintProperty files_in_use;
  // Total number of nodes used as extent containers.
  inspect::UintProperty extent_containers_in_use;
  // Stats about number of extents used per blob. This shows per blob fragmentation of used data
  // blocks. It gives us an idea about fragmentation from blob to blob - some blobs might be more
  // fragmented than the others.
  inspect::ExponentialUintHistogram extents_per_file;
  // Stats about used data blocks fragments. This shows used block fragmentation within Blobfs.
  inspect::ExponentialUintHistogram in_use_fragments;
  // Stats about free data blocks fragments. This provides an important insight into
  // success/failure of OTA.
  inspect::ExponentialUintHistogram free_fragments;
};

// Exact fragmentation statistics that Blobfs calculates. Used for testing/validation purposes.
//
// Although we could construct |FragmentationMetrics| from this data, these statistics can consume
// a lot of memory if the filesystem is heavily fragmented, so this is not used in production.
// |FragmentationMetrics| instead stores these values in histograms, using a fixed amount of memory.
struct FragmentationStats {
  uint64_t total_nodes;
  uint64_t files_in_use;
  uint64_t extent_containers_in_use;
  std::map<size_t, uint64_t> extents_per_file;
  std::map<size_t, uint64_t> free_fragments;
  std::map<size_t, uint64_t> in_use_fragments;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_METRICS_FRAGMENTATION_METRICS_H_
