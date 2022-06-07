// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_BLOBFS_INSPECT_TREE_H_
#define SRC_STORAGE_BLOBFS_BLOBFS_INSPECT_TREE_H_

#include <lib/syslog/cpp/macros.h>
#include <zircon/system/public/zircon/compiler.h>

#include <memory>
#include <mutex>

#include "src/lib/storage/vfs/cpp/inspect/inspect_tree.h"
#include "src/lib/storage/vfs/cpp/inspect/node_operations.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/node_finder.h"

namespace blobfs {

class Blobfs;

// Encapsulates Blobfs fragmentation metrics. Thread-safe.
struct FragmentationMetrics {
  FragmentationMetrics() = default;
  explicit FragmentationMetrics(inspect::Node& root);

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

// Stores total size of all blobs with various compression formats.
struct CompressionStats {
  void Update(const InodePtr& inode);

  uint64_t uncompressed_bytes = 0;
  uint64_t zstd_chunked_bytes = 0;

  // Inspect metrics that map to the above compression stats.
  struct Metrics {
    Metrics() = default;
    Metrics(inspect::Node& root, const CompressionStats& stats);
    inspect::UintProperty uncompressed_bytes;
    inspect::UintProperty zstd_chunked_bytes;
  };
};

// Encapsulates the state required to make a filesystem inspect tree for Blobfs.
class BlobfsInspectTree final {
 public:
  BlobfsInspectTree();
  ~BlobfsInspectTree() = default;

  // Set general filesystem information.
  void SetInfo(const fs_inspect::InfoData& info) __TA_EXCLUDES(info_mutex_);

  // Update resource usage values that change when certain fields in the superblock are modified.
  void UpdateSuperblock(const Superblock& superblock) __TA_EXCLUDES(usage_mutex_);

  // Update FVM volume information and record any out of space events.
  void UpdateVolumeData(const block_client::BlockDevice& device, bool out_of_space = false)
      __TA_EXCLUDES(volume_mutex_);

  // The Inspector this object owns.
  const inspect::Inspector& inspector() { return inspector_; }

  // Node-level operation trackers.
  fs_inspect::NodeOperations& node_operations() { return node_operations_; }

  // Calls |CalculateFragmentationMetrics| on |blobfs| and atomically updates the Inspect tree.
  void CalculateFragmentationMetrics(Blobfs& blobfs);

  // Record updated compression statistics under the compression_metrics node.
  void UpdateCompressionMetrics(const CompressionStats& stats);

 private:
  // Helper function to create and return all required callbacks to create an fs_inspect tree.
  fs_inspect::NodeCallbacks CreateCallbacks();

  //
  // Generic fs_inspect Properties
  //

  mutable std::mutex info_mutex_{};
  fs_inspect::InfoData info_ __TA_GUARDED(info_mutex_){};

  mutable std::mutex usage_mutex_{};
  fs_inspect::UsageData usage_ __TA_GUARDED(usage_mutex_){};

  mutable std::mutex volume_mutex_{};
  fs_inspect::VolumeData volume_ __TA_GUARDED(volume_mutex_){};

  // The Inspector to which the tree is attached.
  inspect::Inspector inspector_;

  // In order to distinguish filesystem instances, we must attach the InspectTree to a uniquely
  // named child node instead of the Inspect root. This is because fshost currently serves all
  // filesystem inspect trees, and is not be required when filesystems are componentized (the tree
  // can be attached directly to the inspect root in that case).
  inspect::Node tree_root_;

  // Node to which operational statistics (latency/error counters) are added.
  inspect::Node opstats_node_;

  // All common filesystem node operation trackers.
  fs_inspect::NodeOperations node_operations_;

  // fs.detail node under which all Blobfs-specific properties are placed.
  inspect::Node detail_node_;

  inspect::Node fragmentation_metrics_node_;
  FragmentationMetrics fragmentation_metrics_;

  inspect::Node compression_metrics_node_;
  CompressionStats::Metrics compression_metrics_;

  // Filesystem inspect tree nodes.
  // **MUST be declared last**, as the callbacks passed to this object use the above properties.
  // This ensures that the callbacks are destroyed before any properties that they may reference.
  fs_inspect::FilesystemNodes fs_inspect_nodes_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_BLOBFS_INSPECT_TREE_H_
