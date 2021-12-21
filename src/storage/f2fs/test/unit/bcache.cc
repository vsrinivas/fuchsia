// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "src/storage/f2fs/f2fs.h"

namespace f2fs {
namespace {

using block_client::FakeBlockDevice;

constexpr uint32_t kMinVolumeSize = 104'857'600;
constexpr uint32_t kNumBlocks = kMinVolumeSize / kDefaultSectorSize;

TEST(BCacheTest, Trim) {
  {
    std::unique_ptr<f2fs::Bcache> bc;
    bool readonly_device = false;
    auto device = std::make_unique<FakeBlockDevice>(FakeBlockDevice::Config{
        .block_count = kNumBlocks, .block_size = kDefaultSectorSize, .supports_trim = false});
    ASSERT_TRUE(device);
    ASSERT_EQ(f2fs::CreateBcache(std::move(device), &readonly_device, &bc), ZX_OK);

    fuchsia_hardware_block_BlockInfo info;
    bc->device()->BlockGetInfo(&info);
    block_t end_blk = static_cast<block_t>(bc->Maxblk() / (kBlockSize / info.block_size));
    ASSERT_EQ(bc->Trim(0, end_blk), ZX_ERR_NOT_SUPPORTED);
  }
  {
    std::unique_ptr<f2fs::Bcache> bc;
    bool readonly_device = false;
    auto device = std::make_unique<FakeBlockDevice>(FakeBlockDevice::Config{
        .block_count = kNumBlocks, .block_size = kDefaultSectorSize, .supports_trim = true});
    ASSERT_TRUE(device);
    ASSERT_EQ(f2fs::CreateBcache(std::move(device), &readonly_device, &bc), ZX_OK);

    fuchsia_hardware_block_BlockInfo info;
    bc->device()->BlockGetInfo(&info);
    block_t end_blk = static_cast<block_t>(bc->Maxblk() / (kBlockSize / info.block_size));
    ASSERT_EQ(bc->Trim(0, end_blk), ZX_OK);
  }
}

TEST(BCacheTest, GetDevice) {
  {
    std::unique_ptr<f2fs::Bcache> bc;
    bool readonly_device = false;
    auto device = std::make_unique<FakeBlockDevice>(FakeBlockDevice::Config{
        .block_count = kNumBlocks, .block_size = kDefaultSectorSize, .supports_trim = false});
    ASSERT_TRUE(device);
    auto device_ptr = device.get();
    ASSERT_EQ(f2fs::CreateBcache(std::move(device), &readonly_device, &bc), ZX_OK);

    block_client::BlockDevice* bcache_device_ptr = bc->device();
    ASSERT_EQ(bcache_device_ptr, device_ptr);

    const block_client::BlockDevice* bcache_const_device_ptr = bc->device();
    ASSERT_EQ(bcache_const_device_ptr, device_ptr);
  }
}

TEST(BCacheTest, PauseResume) {
  {
    std::unique_ptr<f2fs::Bcache> bc;
    bool readonly_device = false;
    auto device = std::make_unique<FakeBlockDevice>(FakeBlockDevice::Config{
        .block_count = kNumBlocks, .block_size = kDefaultSectorSize, .supports_trim = false});
    ASSERT_TRUE(device);
    ASSERT_EQ(f2fs::CreateBcache(std::move(device), &readonly_device, &bc), ZX_OK);

    ASSERT_EQ(bc->DeviceBlockSize(), kDefaultSectorSize);
    bc->Pause();
    bc->Resume();
  }
}

TEST(BCacheTest, Destroy) {
  {
    std::unique_ptr<f2fs::Bcache> bc;
    bool readonly_device = false;
    auto device = std::make_unique<FakeBlockDevice>(FakeBlockDevice::Config{
        .block_count = kNumBlocks, .block_size = kDefaultSectorSize, .supports_trim = false});
    ASSERT_TRUE(device);
    ASSERT_EQ(f2fs::CreateBcache(std::move(device), &readonly_device, &bc), ZX_OK);

    ASSERT_EQ(bc->DeviceBlockSize(), kDefaultSectorSize);
    __UNUSED auto unused = f2fs::Bcache::Destroy(std::move(bc));
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
    ASSERT_EQ(f2fs::CreateBcache(std::move(device), &readonly_device, &bc), ZX_ERR_NO_RESOURCES);
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
    ASSERT_EQ(f2fs::CreateBcache(std::move(device), &readonly_device, &bc), ZX_ERR_OUT_OF_RANGE);
  }
}

}  // namespace
}  // namespace f2fs
