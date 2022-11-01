// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/bcache.h"

#include <gtest/gtest.h>

#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "src/storage/f2fs/f2fs_layout.h"

namespace f2fs {
namespace {

using block_client::FakeBlockDevice;

constexpr uint32_t kMinVolumeSize = 104'857'600;
constexpr uint32_t kNumBlocks = kMinVolumeSize / kDefaultSectorSize;

TEST(BCacheTest, Trim) {
  {
    bool readonly_device = false;
    auto device = std::make_unique<FakeBlockDevice>(FakeBlockDevice::Config{
        .block_count = kNumBlocks, .block_size = kDefaultSectorSize, .supports_trim = false});
    ASSERT_TRUE(device);
    auto bc_or = CreateBcache(std::move(device), &readonly_device);
    ASSERT_TRUE(bc_or.is_ok());

    fuchsia_hardware_block::wire::BlockInfo info;
    bc_or->GetDevice()->BlockGetInfo(&info);
    block_t end_blk = static_cast<block_t>(bc_or->Maxblk() / (kBlockSize / info.block_size));
    ASSERT_EQ(bc_or->Trim(0, end_blk), ZX_ERR_NOT_SUPPORTED);
  }
  {
    bool readonly_device = false;
    auto device = std::make_unique<FakeBlockDevice>(FakeBlockDevice::Config{
        .block_count = kNumBlocks, .block_size = kDefaultSectorSize, .supports_trim = true});
    ASSERT_TRUE(device);
    auto bc_or = CreateBcache(std::move(device), &readonly_device);
    ASSERT_TRUE(bc_or.is_ok());

    fuchsia_hardware_block::wire::BlockInfo info;
    bc_or->GetDevice()->BlockGetInfo(&info);
    block_t end_blk = static_cast<block_t>(bc_or->Maxblk() / (kBlockSize / info.block_size));
    ASSERT_EQ(bc_or->Trim(0, end_blk), ZX_OK);
  }
}

TEST(BCacheTest, GetDevice) {
  bool readonly_device = false;
  auto device = std::make_unique<FakeBlockDevice>(FakeBlockDevice::Config{
      .block_count = kNumBlocks, .block_size = kDefaultSectorSize, .supports_trim = false});
  ASSERT_TRUE(device);
  auto device_ptr = device.get();
  auto bc_or = CreateBcache(std::move(device), &readonly_device);
  ASSERT_TRUE(bc_or.is_ok());

  block_client::BlockDevice* bcache_device_ptr = bc_or->GetDevice();
  ASSERT_EQ(bcache_device_ptr, device_ptr);

  const block_client::BlockDevice* bcache_const_device_ptr = bc_or->GetDevice();
  ASSERT_EQ(bcache_const_device_ptr, device_ptr);
}

TEST(BCacheTest, PauseResume) {
  {
    bool readonly_device = false;
    auto device = std::make_unique<FakeBlockDevice>(FakeBlockDevice::Config{
        .block_count = kNumBlocks, .block_size = kDefaultSectorSize, .supports_trim = false});
    ASSERT_TRUE(device);
    auto bc_or = CreateBcache(std::move(device), &readonly_device);
    ASSERT_TRUE(bc_or.is_ok());

    ASSERT_EQ(bc_or->DeviceBlockSize(), kDefaultSectorSize);
    bc_or->Pause();
    bc_or->Resume();
  }
}

TEST(BCacheTest, Destroy) {
  {
    bool readonly_device = false;
    auto device = std::make_unique<FakeBlockDevice>(FakeBlockDevice::Config{
        .block_count = kNumBlocks, .block_size = kDefaultSectorSize, .supports_trim = false});
    ASSERT_TRUE(device);
    auto bc_or = CreateBcache(std::move(device), &readonly_device);
    ASSERT_TRUE(bc_or.is_ok());

    ASSERT_EQ(bc_or->DeviceBlockSize(), kDefaultSectorSize);
    [[maybe_unused]] auto unused = f2fs::Bcache::Destroy(std::move(*bc_or));
  }
}

TEST(BCacheTest, Exception) {
  // Test zero block_size exception case
  {
    std::unique_ptr<f2fs::Bcache> bc;
    bool readonly_device = false;
    auto device = std::make_unique<FakeBlockDevice>(FakeBlockDevice::Config{
        .block_count = kNumBlocks, .block_size = 0, .supports_trim = false});
    ASSERT_TRUE(device);
    auto bc_or = CreateBcache(std::move(device), &readonly_device);
    ASSERT_TRUE(bc_or.is_error());
    ASSERT_EQ(bc_or.status_value(), ZX_ERR_NO_SPACE);
  }
  // Test block_count overflow exception case
  {
    std::unique_ptr<f2fs::Bcache> bc;
    bool readonly_device = false;
    auto device = std::make_unique<FakeBlockDevice>(FakeBlockDevice::Config{
        .block_count = static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) * 8,
        .block_size = kDefaultSectorSize,
        .supports_trim = true});
    ASSERT_TRUE(device);
    auto bc_or = CreateBcache(std::move(device), &readonly_device);
    ASSERT_TRUE(bc_or.is_error());
    ASSERT_EQ(bc_or.status_value(), ZX_ERR_OUT_OF_RANGE);
  }
}

}  // namespace
}  // namespace f2fs
