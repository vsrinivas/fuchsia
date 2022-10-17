// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/inspect/inspect_data.h"

#include <fuchsia/hardware/block/c/fidl.h>

namespace fs_inspect {

namespace detail {

void Attach(inspect::Inspector& insp, const InfoData& info) {
  inspect::Node& root = insp.GetRoot();

  root.CreateUint(InfoData::kPropId, info.id, &insp);
  root.CreateUint(InfoData::kPropType, info.type, &insp);
  root.CreateString(InfoData::kPropName, info.name, &insp);
  root.CreateUint(InfoData::kPropVersionMajor, info.version_major, &insp);
  root.CreateUint(InfoData::kPropVersionMinor, info.version_minor, &insp);
  root.CreateUint(InfoData::kPropBlockSize, info.block_size, &insp);
  root.CreateUint(InfoData::kPropMaxFilenameLength, info.max_filename_length, &insp);
  if (info.oldest_version.has_value()) {
    root.CreateString(InfoData::kPropOldestVersion, info.oldest_version.value(), &insp);
  }
}

void Attach(inspect::Inspector& insp, const UsageData& usage) {
  inspect::Node& root = insp.GetRoot();

  root.CreateUint(UsageData::kPropTotalBytes, usage.total_bytes, &insp);
  root.CreateUint(UsageData::kPropUsedBytes, usage.used_bytes, &insp);
  root.CreateUint(UsageData::kPropTotalNodes, usage.total_nodes, &insp);
  root.CreateUint(UsageData::kPropUsedNodes, usage.used_nodes, &insp);
}

void Attach(inspect::Inspector& insp, const FvmData& volume) {
  inspect::Node& root = insp.GetRoot();

  root.CreateUint(FvmData::kPropSizeBytes, volume.size_info.size_bytes, &insp);
  root.CreateUint(FvmData::kPropSizeLimitBytes, volume.size_info.size_limit_bytes, &insp);
  root.CreateUint(FvmData::kPropAvailableSpaceBytes, volume.size_info.available_space_bytes, &insp);
  root.CreateUint(FvmData::kPropOutOfSpaceEvents, volume.out_of_space_events, &insp);
}

}  // namespace detail

std::string InfoData::OldestVersion(uint32_t oldest_major, uint32_t oldest_minor) {
  return std::to_string(oldest_major) + "/" + std::to_string(oldest_minor);
}

zx::result<FvmData::SizeInfo> FvmData::GetSizeInfoFromDevice(
    const block_client::BlockDevice& device) {
  FvmData::SizeInfo size_info{};
  // This information is for the entire FVM volume. So the "slices allocated" counts across all
  // partitions inside of FVM.
  fuchsia_hardware_block_volume_VolumeManagerInfo volume_manager;
  fuchsia_hardware_block_volume_VolumeInfo volume_info;
  zx_status_t status = device.VolumeGetInfo(&volume_manager, &volume_info);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  size_info.size_bytes = volume_info.partition_slice_count * volume_manager.slice_size;
  size_info.size_limit_bytes = volume_info.slice_limit * volume_manager.slice_size;
  size_info.available_space_bytes =
      (volume_manager.slice_count - volume_manager.assigned_slice_count) *
      volume_manager.slice_size;

  // If the volume has a size limit set, make sure free_space_bytes accurately reflects that.
  if (size_info.size_limit_bytes > 0) {
    // The partition may be larger than this limit if a smaller limit was applied after the
    // partition had already grown to the current size.
    if (size_info.size_bytes > size_info.size_limit_bytes) {
      size_info.available_space_bytes = 0;
    } else {
      size_info.available_space_bytes = std::min(size_info.available_space_bytes,
                                                 size_info.size_limit_bytes - size_info.size_bytes);
    }
  }

  return zx::ok(size_info);
}

}  // namespace fs_inspect
