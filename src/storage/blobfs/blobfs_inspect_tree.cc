// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/blobfs_inspect_tree.h"

#include <atomic>

#include "src/storage/blobfs/blobfs.h"

namespace {

inspect::Inspector CreateInspector() {
#ifdef BLOBFS_ENABLE_LARGE_INSPECT_VMO
  // When recording page-in frequencies, a much larger Inspect VMO is required (>512KB).
  //
  // TODO(fxbug.dev/59043): Inspect should print warnings about overflowing the maximum size of a
  // VMO.
  const size_t kMaxInspectVmoSize = 2UL * 1024UL * 1024UL;
  return inspect::Inspector(inspect::InspectSettings{.maximum_size = kMaxInspectVmoSize});
#else
  // Use default inspect settings (currently sets the Inspect VMO size to 256KiB).
  return inspect::Inspector();
#endif
}

}  // namespace

namespace blobfs {

BlobfsInspectTree::BlobfsInspectTree()
    : inspector_(CreateInspector()),
      tree_root_(inspector_.GetRoot().CreateChild("blobfs")),
      opstats_node_(tree_root_.CreateChild("fs.opstats")),
      node_operations_(opstats_node_),
      detail_node_(tree_root_.CreateChild(fs_inspect::kDetailNodeName)),
      fragmentation_metrics_node_(detail_node_.CreateChild("fragmentation_metrics")),
      compression_metrics_node_(detail_node_.CreateChild("compression_metrics")),
      fs_inspect_nodes_(fs_inspect::CreateTree(tree_root_, CreateCallbacks())) {}

// Set general filesystem information.
void BlobfsInspectTree::SetInfo(const fs_inspect::InfoData& info) {
  std::lock_guard guard(info_mutex_);
  info_ = info;
}

// Update resource usage values that change when certain fields in the superblock are modified.
void BlobfsInspectTree::UpdateSuperblock(const Superblock& superblock) {
  std::lock_guard guard(usage_mutex_);
  usage_.total_bytes = superblock.data_block_count * superblock.block_size;
  usage_.used_bytes = superblock.alloc_block_count * superblock.block_size;
  usage_.total_nodes = superblock.inode_count;
  usage_.used_nodes = superblock.alloc_inode_count;
}

// Update FVM fvm information and record any out of space events.
void BlobfsInspectTree::UpdateFvmData(const block_client::BlockDevice& device, bool out_of_space) {
  zx::result<fs_inspect::FvmData::SizeInfo> size_info =
      fs_inspect::FvmData::GetSizeInfoFromDevice(device);
  if (size_info.is_error()) {
    FX_LOGS(WARNING) << "Failed to obtain size information from block device: "
                     << size_info.status_string();
  }
  std::lock_guard guard(fvm_mutex_);
  if (size_info.is_ok()) {
    fvm_.size_info = size_info.value();
  }
  if (out_of_space) {
    ++fvm_.out_of_space_events;
  }
}

fs_inspect::NodeCallbacks BlobfsInspectTree::CreateCallbacks() {
  return {
      .info_callback =
          [this] {
            std::lock_guard guard(info_mutex_);
            return info_;
          },
      .usage_callback =
          [this] {
            std::lock_guard guard(usage_mutex_);
            return usage_;
          },
      .fvm_callback =
          [this] {
            std::lock_guard guard(fvm_mutex_);
            return fvm_;
          },
  };
}

void BlobfsInspectTree::CalculateFragmentationMetrics(Blobfs& blobfs) {
  fragmentation_metrics_node_.AtomicUpdate([this, &blobfs](inspect::Node& node) {
    fragmentation_metrics_ = FragmentationMetrics(node);
    blobfs.CalculateFragmentationMetrics(fragmentation_metrics_);
  });
}

void BlobfsInspectTree::UpdateCompressionMetrics(const CompressionMetrics& metrics) {
  compression_metrics_node_.AtomicUpdate(
      [this, &metrics](inspect::Node& node) { compression_metrics_ = metrics.Attach(node); });
}

}  // namespace blobfs
