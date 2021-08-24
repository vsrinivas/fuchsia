// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <block-client/cpp/fake-device.h>
#include <gtest/gtest.h>

#include "third_party/f2fs/f2fs.h"

namespace f2fs {
namespace {

using block_client::FakeBlockDevice;
constexpr uint32_t kNumBlocks = kMinVolumeSize / kBlockSize;

TEST(BCacheTest, TrimTest) {
  {
    std::unique_ptr<f2fs::Bcache> bc;
    bool readonly_device = false;
    auto device = std::make_unique<FakeBlockDevice>(FakeBlockDevice::Config{
        .block_count = kNumBlocks, .block_size = kDefaultSectorSize, .supports_trim = false});
    ASSERT_TRUE(device);
    ASSERT_EQ(f2fs::CreateBcache(std::move(device), &readonly_device, &bc), ZX_OK);
    ASSERT_EQ(bc->Trim(0, bc->Maxblk()), ZX_ERR_NOT_SUPPORTED);
  }
  {
    std::unique_ptr<f2fs::Bcache> bc;
    bool readonly_device = false;
    auto device = std::make_unique<FakeBlockDevice>(FakeBlockDevice::Config{
        .block_count = kNumBlocks, .block_size = kDefaultSectorSize, .supports_trim = true});
    ASSERT_TRUE(device);
    ASSERT_EQ(f2fs::CreateBcache(std::move(device), &readonly_device, &bc), ZX_OK);
    ASSERT_EQ(bc->Trim(0, bc->Maxblk()), ZX_OK);
  }
}

}  // namespace
}  // namespace f2fs
