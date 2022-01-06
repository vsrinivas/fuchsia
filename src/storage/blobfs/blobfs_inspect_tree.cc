// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/blobfs_inspect_tree.h"

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

// Update FVM volume information and record any out of space events.
void BlobfsInspectTree::UpdateVolumeData(const block_client::BlockDevice& device,
                                         bool out_of_space) {
  zx::status<fs_inspect::VolumeData::SizeInfo> size_info =
      fs_inspect::VolumeData::GetSizeInfoFromDevice(device);
  if (size_info.is_error()) {
    FX_LOGS(WARNING) << "Failed to obtain size information from block device: "
                     << size_info.status_string();
  }
  std::lock_guard guard(volume_mutex_);
  if (size_info.is_ok()) {
    volume_.size_info = size_info.value();
  }
  if (out_of_space) {
    ++volume_.out_of_space_events;
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
      .volume_callback =
          [this] {
            std::lock_guard guard(volume_mutex_);
            return volume_;
          },
  };
}

}  // namespace blobfs
