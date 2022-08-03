// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/fuse-config.h"

#include <lib/mmio/mmio.h>

#include <cstdint>

#include <gtest/gtest.h>

#include "src/graphics/display/drivers/intel-i915-tgl/mock-mmio-range.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers.h"

namespace i915_tgl {

namespace {

class FuseConfigTest : public ::testing::Test {
 public:
  FuseConfigTest() = default;
  ~FuseConfigTest() override = default;

  void TearDown() override { mmio_range_.CheckAllAccessesReplayed(); }

 protected:
  constexpr static int kMmioRangeSize = 0x60000;
  MockMmioRange mmio_range_{kMmioRangeSize, MockMmioRange::Size::k32};
  fdf::MmioBuffer mmio_buffer_{mmio_range_.GetMmioBuffer()};
};

TEST_F(FuseConfigTest, TigerLakeDefault) {
  // Tiger Lake: IHD-OS-TGL-Vol 2c-12.21 Part 1 pages 432-434
  constexpr int kDfsmOffset = 0x51000;
  constexpr int kNuc11GpuDeviceId = 0x9a49;

  mmio_range_.Expect({.address = kDfsmOffset, .value = 0});
  FuseConfig fuse_config = FuseConfig::ReadFrom(mmio_buffer_, kNuc11GpuDeviceId);

  EXPECT_EQ(652'800, fuse_config.core_clock_limit_khz);
  EXPECT_EQ(true, fuse_config.graphics_enabled);
  EXPECT_EQ(true, fuse_config.pipe_enabled[0]);
  EXPECT_EQ(true, fuse_config.pipe_enabled[1]);
  EXPECT_EQ(true, fuse_config.pipe_enabled[2]);
  EXPECT_EQ(true, fuse_config.pipe_enabled[3]);
  EXPECT_EQ(true, fuse_config.edp_enabled);
  EXPECT_EQ(true, fuse_config.display_capture_enabled);
  EXPECT_EQ(true, fuse_config.display_stream_compression_enabled);
  EXPECT_EQ(true, fuse_config.frame_buffer_compression_enabled);
  EXPECT_EQ(true, fuse_config.display_power_savings_enabled);
}

TEST_F(FuseConfigTest, TigerLakeAllFusesBlown) {
  // Tiger Lake: IHD-OS-TGL-Vol 2c-12.21 Part 1 pages 432-434
  constexpr int kDfsmOffset = 0x51000;
  constexpr int kNuc11GpuDeviceId = 0x9a49;

  mmio_range_.Expect({.address = kDfsmOffset, .value = 0xffffffff});
  FuseConfig fuse_config = FuseConfig::ReadFrom(mmio_buffer_, kNuc11GpuDeviceId);

  EXPECT_EQ(652'800, fuse_config.core_clock_limit_khz);
  EXPECT_EQ(true, fuse_config.graphics_enabled);
  EXPECT_EQ(false, fuse_config.pipe_enabled[0]);
  EXPECT_EQ(false, fuse_config.pipe_enabled[1]);
  EXPECT_EQ(false, fuse_config.pipe_enabled[2]);
  EXPECT_EQ(false, fuse_config.pipe_enabled[3]);
  EXPECT_EQ(false, fuse_config.edp_enabled);
  EXPECT_EQ(false, fuse_config.display_capture_enabled);
  EXPECT_EQ(false, fuse_config.display_stream_compression_enabled);
  EXPECT_EQ(false, fuse_config.frame_buffer_compression_enabled);
  EXPECT_EQ(false, fuse_config.display_power_savings_enabled);
}

TEST_F(FuseConfigTest, KabyLakeDefault) {
  // Tiger Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 pages 497-499
  constexpr int kDfsmOffset = 0x51000;
  constexpr int kAtlasGpuDeviceId = 0x591c;

  mmio_range_.Expect({.address = kDfsmOffset, .value = 0});
  FuseConfig fuse_config = FuseConfig::ReadFrom(mmio_buffer_, kAtlasGpuDeviceId);

  EXPECT_EQ(675'000, fuse_config.core_clock_limit_khz);
  EXPECT_EQ(true, fuse_config.graphics_enabled);
  EXPECT_EQ(true, fuse_config.pipe_enabled[0]);
  EXPECT_EQ(true, fuse_config.pipe_enabled[1]);
  EXPECT_EQ(true, fuse_config.pipe_enabled[2]);
  EXPECT_EQ(false, fuse_config.pipe_enabled[3]);
  EXPECT_EQ(true, fuse_config.edp_enabled);
  EXPECT_EQ(true, fuse_config.display_capture_enabled);
  EXPECT_EQ(true, fuse_config.display_stream_compression_enabled);
  EXPECT_EQ(true, fuse_config.frame_buffer_compression_enabled);
  EXPECT_EQ(true, fuse_config.display_power_savings_enabled);
}

TEST_F(FuseConfigTest, KabyLakeAllFusesBlown) {
  // Tiger Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 pages 497-499
  constexpr int kDfsmOffset = 0x51000;
  constexpr int kAtlasGpuDeviceId = 0x591c;

  mmio_range_.Expect({.address = kDfsmOffset, .value = 0xffffffff});
  FuseConfig fuse_config = FuseConfig::ReadFrom(mmio_buffer_, kAtlasGpuDeviceId);

  EXPECT_EQ(337'500, fuse_config.core_clock_limit_khz);
  EXPECT_EQ(false, fuse_config.graphics_enabled);
  EXPECT_EQ(false, fuse_config.pipe_enabled[0]);
  EXPECT_EQ(false, fuse_config.pipe_enabled[1]);
  EXPECT_EQ(false, fuse_config.pipe_enabled[2]);
  EXPECT_EQ(false, fuse_config.pipe_enabled[3]);
  EXPECT_EQ(false, fuse_config.edp_enabled);
  EXPECT_EQ(false, fuse_config.display_capture_enabled);
  EXPECT_EQ(true, fuse_config.display_stream_compression_enabled);
  EXPECT_EQ(false, fuse_config.frame_buffer_compression_enabled);
  EXPECT_EQ(false, fuse_config.display_power_savings_enabled);
}

}  // namespace

}  // namespace i915_tgl
