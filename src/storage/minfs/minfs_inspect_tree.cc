// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/minfs_inspect_tree.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>

#include <safemath/checked_math.h>

namespace minfs {

fs_inspect::UsageData CalculateSpaceUsage(const Superblock& superblock, uint64_t reserved_blocks) {
  return {
      .total_bytes = static_cast<uint64_t>(superblock.block_count) * superblock.block_size,
      .used_bytes = (superblock.alloc_block_count + reserved_blocks) * superblock.block_size,
      .total_nodes = superblock.inode_count,
      .used_nodes = superblock.alloc_inode_count,
  };
}

MinfsInspectTree::MinfsInspectTree(const block_client::BlockDevice* device)
    : device_(device),
      tree_root_(inspector_.GetRoot().CreateChild("minfs")),
      opstats_node_(tree_root_.CreateChild("fs.opstats")),
      node_operations_(opstats_node_) {
  ZX_ASSERT(device_);
  inspector_.CreateStatsNode();
}

void MinfsInspectTree::Initialize(const fs::FilesystemInfo& fs_info, const Superblock& superblock,
                                  uint64_t reserved_blocks) {
  // Set initial data for the fs.info and fs.usage nodes.
  {
    std::lock_guard guard(info_mutex_);
    info_ = {
        .id = fs_info.fs_id,
        .type = fs_info.fs_type,
        .name = fs_info.name,
        .version_major = kMinfsCurrentMajorVersion,
        .version_minor = kMinfsCurrentMinorVersion,
        .block_size = fs_info.block_size,
        .max_filename_length = fs_info.max_filename_size,
        .oldest_version = fs_inspect::InfoData::OldestVersion(superblock.major_version,
                                                              superblock.oldest_minor_version),
    };
  }
  UpdateSpaceUsage(superblock, reserved_blocks);
  fs_inspect_nodes_ = fs_inspect::CreateTree(tree_root_, CreateCallbacks());
}

void MinfsInspectTree::UpdateSpaceUsage(const Superblock& superblock, uint64_t reserved_blocks) {
  std::lock_guard guard(usage_mutex_);
  usage_ = CalculateSpaceUsage(superblock, reserved_blocks);
}

void MinfsInspectTree::OnOutOfSpace() {
  zx::time curr_time = zx::clock::get_monotonic();
  std::lock_guard guard(volume_mutex_);
  if ((curr_time - last_out_of_space_event_) > kEventWindowDuration) {
    ++volume_.out_of_space_events;
    last_out_of_space_event_ = curr_time;
  }
}

void MinfsInspectTree::OnRecoveredSpace() {
  zx::time curr_time = zx::clock::get_monotonic();
  std::lock_guard guard(volume_mutex_);
  if ((curr_time - last_recovered_space_event_) > kEventWindowDuration) {
    ++recovered_space_events_;
    last_recovered_space_event_ = curr_time;
  }
}

void MinfsInspectTree::AddDirtyBytes(uint64_t bytes) {
  std::lock_guard guard(volume_mutex_);
  dirty_bytes_ = safemath::CheckAdd(dirty_bytes_, bytes).ValueOrDie();
}

void MinfsInspectTree::SubtractDirtyBytes(uint64_t bytes) {
  std::lock_guard guard(volume_mutex_);
  dirty_bytes_ = safemath::CheckSub(dirty_bytes_, bytes).ValueOrDie();
}

fs_inspect::VolumeData MinfsInspectTree::GetVolumeData() {
  zx::status<fs_inspect::VolumeData::SizeInfo> size_info = zx::error(ZX_ERR_BAD_HANDLE);
  {
    std::lock_guard guard(device_mutex_);
    size_info = fs_inspect::VolumeData::GetSizeInfoFromDevice(*device_);
    if (size_info.is_error()) {
      FX_LOGS(WARNING) << "Failed to obtain size information from block device: "
                       << size_info.status_string();
    }
  }
  std::lock_guard guard(volume_mutex_);
  if (size_info.is_ok()) {
    volume_.size_info = size_info.value();
  }
  return volume_;
}

inspect::LazyNodeCallbackFn MinfsInspectTree::CreateDetailNode() const {
  return [this]() {
    inspect::Inspector insp;
    uint64_t recovered_space_events;
    uint64_t dirty_bytes;
    {
      std::lock_guard guard(volume_mutex_);
      recovered_space_events = recovered_space_events_;
      dirty_bytes = dirty_bytes_;
    }
    insp.GetRoot().CreateUint("recovered_space_events", recovered_space_events, &insp);
    insp.GetRoot().CreateUint("dirty_bytes", dirty_bytes, &insp);
    return fpromise::make_ok_promise(insp);
  };
}

fs_inspect::NodeCallbacks MinfsInspectTree::CreateCallbacks() {
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
      .volume_callback = [this] { return GetVolumeData(); },
      .detail_node_callback = CreateDetailNode(),
  };
}

}  // namespace minfs
