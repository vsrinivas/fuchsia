// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

InspectTree::InspectTree(F2fs* fs) : fs_(fs) { ZX_ASSERT(fs_ != nullptr); }

void InspectTree::Initialize() {
  zx::status<fs::FilesystemInfo> fs_info = fs_->GetFilesystemInfo();
  if (fs_info.is_error()) {
    FX_LOGS(ERROR) << "Failed to initialize F2fs inspect tree: GetFilesystemInfo returned "
                   << fs_info.status_string();
    return;
  }

  {
    std::lock_guard guard(info_mutex_);
    info_ = {
        .id = fs_info.value().fs_id,
        .type = fidl::ToUnderlying(fs_info.value().fs_type),
        .name = fs_info.value().name,
        .version_major = fs_->GetSuperblockInfo().GetRawSuperblock().major_ver,
        .version_minor = fs_->GetSuperblockInfo().GetRawSuperblock().minor_ver,
        .block_size = fs_info.value().block_size,
        .max_filename_length = fs_info.value().max_filename_size,
    };
  }

  {
    std::lock_guard guard(usage_mutex_);
    UpdateUsage();
  }

  {
    std::lock_guard guard(volume_mutex_);
    UpdateVolumeSizeInfo();
  }

  tree_root_ = inspector_.GetRoot().CreateChild("f2fs");
  fs_inspect_nodes_ = fs_inspect::CreateTree(tree_root_, CreateCallbacks());
  inspector_.CreateStatsNode();
}

void InspectTree::UpdateUsage() {
  zx::status<fs::FilesystemInfo> fs_info = fs_->GetFilesystemInfo();
  if (fs_info.is_error()) {
    FX_LOGS(ERROR) << "Failed to initialize F2fs inspect tree: GetFilesystemInfo returned "
                   << fs_info.status_string();
    return;
  }

  usage_.total_bytes = fs_info.value().total_bytes;
  usage_.used_bytes = fs_info.value().used_bytes;
  usage_.total_nodes = fs_info.value().total_nodes;
  usage_.used_nodes = fs_info.value().used_nodes;
}

void InspectTree::UpdateVolumeSizeInfo() {
  zx::status<fs_inspect::VolumeData::SizeInfo> size_info = zx::error(ZX_ERR_BAD_HANDLE);
  {
    size_info = fs_inspect::VolumeData::GetSizeInfoFromDevice(*fs_->GetBc().GetDevice());
    if (size_info.is_error()) {
      FX_LOGS(WARNING) << "Failed to obtain size information from block device: "
                       << size_info.status_string();
    }
  }

  if (size_info.is_ok()) {
    volume_.size_info = size_info.value();
  }
}

void InspectTree::OnOutOfSpace() {
  zx::time curr_time = zx::clock::get_monotonic();
  std::lock_guard guard(volume_mutex_);
  if ((curr_time - last_out_of_space_time_) > kOutOfSpaceDuration) {
    ++volume_.out_of_space_events;
    last_out_of_space_time_ = curr_time;
  }
}

fs_inspect::NodeCallbacks InspectTree::CreateCallbacks() {
  return {
      .info_callback =
          [this] {
            std::lock_guard guard(info_mutex_);
            return info_;
          },
      .usage_callback =
          [this] {
            std::lock_guard guard(usage_mutex_);
            UpdateUsage();
            return usage_;
          },
      .volume_callback =
          [this] {
            std::lock_guard guard(volume_mutex_);
            UpdateVolumeSizeInfo();
            return volume_;
          },
  };
}

}  // namespace f2fs
