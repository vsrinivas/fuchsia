// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/result.h>

#include <optional>

#include <gtest/gtest.h>

#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "src/lib/storage/vfs/cpp/inspect/inspect_data.h"

using VolumeManagerInfo = fuchsia_hardware_block_volume::wire::VolumeManagerInfo;
using VolumeInfo = fuchsia_hardware_block_volume::wire::VolumeInfo;

namespace fs_inspect {

// Fake block device used to validate calculation of reported size information under fs.volume.
class FakeBlockDevice final : public block_client::FakeBlockDevice {
 public:
  FakeBlockDevice() : block_client::FakeBlockDevice({}) {}
  ~FakeBlockDevice() override = default;

  zx_status_t VolumeGetInfo(
      fuchsia_hardware_block_volume::wire::VolumeManagerInfo* out_manager_info,
      fuchsia_hardware_block_volume::wire::VolumeInfo* out_volume_info) const final {
    *out_manager_info = volume_manager_info_;
    *out_volume_info = volume_info_;
    return ZX_OK;
  }

  void SetVolumeInfo(const VolumeManagerInfo& volume_manager_info, const VolumeInfo& volume_info) {
    volume_manager_info_ = volume_manager_info;
    volume_info_ = volume_info;
  }

 private:
  VolumeManagerInfo volume_manager_info_;
  VolumeInfo volume_info_;
};

// Validates calculation of size information via `FvmData::GetSizeInfoFromDevice` ensuring that
// it is consistent with the values reported by the underlying block device.
TEST(VfsInspectData, GetSizeInfoFromDevice) {
  const uint64_t kSliceSize = 1024;

  VolumeManagerInfo volume_manager_info = {
      .slice_size = kSliceSize,
      .slice_count = 50,           // Slices volume manager can use
      .assigned_slice_count = 20,  // Slices currently in use by partitions
  };

  VolumeInfo volume_info = {
      .partition_slice_count = 5,  // Slices allocated to the filesystem volume
      .slice_limit = 0,  // Size limit set by `VolumeManager.SetPartitionLimit()` (0 = no limit)
  };

  FvmData::SizeInfo expected_size_info = {
      // The partition size is the slice count times slice size
      .size_bytes = volume_info.partition_slice_count * kSliceSize,
      // We haven't set a slice limit above, so ensure that's indicated below
      .size_limit_bytes = 0,
      // The available space to grow in the currently allocated volume is the difference of how many
      // slices are currently allocated to the volume manager minus those assigned to the volume
      .available_space_bytes =
          (volume_manager_info.slice_count - volume_manager_info.assigned_slice_count) * kSliceSize,
  };

  FakeBlockDevice fake_device;
  zx::result<FvmData::SizeInfo> size_info;

  fake_device.SetVolumeInfo(volume_manager_info, volume_info);
  size_info = FvmData::GetSizeInfoFromDevice(fake_device);
  ASSERT_TRUE(size_info.is_ok());
  EXPECT_EQ(expected_size_info.size_bytes, size_info->size_bytes);
  EXPECT_EQ(expected_size_info.size_limit_bytes, size_info->size_limit_bytes);
  EXPECT_EQ(expected_size_info.available_space_bytes, size_info->available_space_bytes);

  // Set a slice limit on the volume and make sure that gets reflected accordingly.
  volume_info.slice_limit = 10;
  expected_size_info.size_limit_bytes = volume_info.slice_limit * kSliceSize;
  expected_size_info.available_space_bytes =
      (volume_info.slice_limit - volume_info.partition_slice_count) * kSliceSize;

  fake_device.SetVolumeInfo(volume_manager_info, volume_info);
  size_info = FvmData::GetSizeInfoFromDevice(fake_device);
  ASSERT_TRUE(size_info.is_ok());
  EXPECT_EQ(expected_size_info.size_bytes, size_info->size_bytes);
  EXPECT_EQ(expected_size_info.size_limit_bytes, size_info->size_limit_bytes);
  EXPECT_EQ(expected_size_info.available_space_bytes, size_info->available_space_bytes);

  // If the slice limit is smaller than the current partition size, ensure that the available free
  // space reflects that even if the volume manager can grow larger.
  volume_info.slice_limit = 2;
  expected_size_info.size_limit_bytes = volume_info.slice_limit * kSliceSize;
  expected_size_info.available_space_bytes = 0;

  fake_device.SetVolumeInfo(volume_manager_info, volume_info);
  size_info = FvmData::GetSizeInfoFromDevice(fake_device);
  ASSERT_TRUE(size_info.is_ok());
  EXPECT_EQ(expected_size_info.size_bytes, size_info->size_bytes);
  EXPECT_EQ(expected_size_info.size_limit_bytes, size_info->size_limit_bytes);
  EXPECT_EQ(expected_size_info.available_space_bytes, size_info->available_space_bytes);
}

}  // namespace fs_inspect
