// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915/pipe.h"

#include <lib/mmio-ptr/fake.h>
#include <lib/mmio/mmio.h>
#include <zircon/pixelformat.h>

#include <memory>
#include <vector>

#include <fake-mmio-reg/fake-mmio-reg.h>
#include <gtest/gtest.h>

#include "src/graphics/display/drivers/intel-i915/registers-pipe.h"

namespace i915 {

class PipeTest : public ::testing::Test {
 public:
  PipeTest() = default;

  void SetUp() override {
    regs_.resize(kMinimumRegCount);
    reg_region_ = std::make_unique<ddk_fake::FakeMmioRegRegion>(regs_.data(), sizeof(uint32_t),
                                                                kMinimumRegCount);
    mmio_buffer_.emplace(reg_region_->GetMmioBuffer());
  }

  void TearDown() override {}

 protected:
  constexpr static uint32_t kMinimumRegCount = 0xd0000 / sizeof(uint32_t);
  std::unique_ptr<ddk_fake::FakeMmioRegRegion> reg_region_;
  std::vector<ddk_fake::FakeMmioReg> regs_;
  std::optional<fdf::MmioBuffer> mmio_buffer_;
};

namespace {

class TestGttRegionImpl : public GttRegion {
 public:
  explicit TestGttRegionImpl(uint64_t handle) : handle_(handle) {}

  uint64_t bytes_per_row() const override { return 64; }
  uint64_t base() const override { return handle_ + 0xf0000000; }

 private:
  uint64_t handle_ = 0;
};

std::map<uint64_t, TestGttRegionImpl> region_map;

const GttRegion& GetGttImageHandle(const image_t* image, uint32_t rotation) {
  auto it = region_map.find(image->handle);
  if (it != region_map.end()) {
    return it->second;
  }
  return region_map.try_emplace(image->handle, image->handle).first->second;
}

layer_t CreatePrimaryLayerConfig(uint64_t handle, uint32_t z_index = 1u) {
  uint32_t kWidth = 1024u;
  uint32_t kHeight = 768u;

  layer_t layer;
  layer.type = LAYER_TYPE_PRIMARY;
  layer.z_index = z_index;
  layer.cfg.primary = {
      .image =
          {
              .width = kWidth,
              .height = kHeight,
              .pixel_format = ZX_PIXEL_FORMAT_ARGB_8888,
              .type = IMAGE_TYPE_SIMPLE,
              .handle = handle,
          },
      .alpha_mode = ALPHA_DISABLE,
      .transform_mode = FRAME_TRANSFORM_IDENTITY,
      .src_frame = {0, 0, kWidth, kHeight},
      .dest_frame = {0, 0, kWidth, kHeight},
  };
  return layer;
}

}  // namespace

// Verifies that GetVsyncConfigStamp() could return the correct config stamp
// given different image handles from device registers.
TEST_F(PipeTest, GetVsyncConfigStamp) {
  Pipe pipe(&*mmio_buffer_, registers::Pipe::PIPE_A, {});

  uint64_t kImageHandle1 = 0x1111u;
  uint64_t kImageHandle2 = 0x2222u;
  uint64_t kImageHandle3 = 0x3333u;
  layer_t layer_1 = CreatePrimaryLayerConfig(kImageHandle1, 1u);
  layer_t layer_2 = CreatePrimaryLayerConfig(kImageHandle2, 1u);
  layer_t layer_3 = CreatePrimaryLayerConfig(kImageHandle3, 2u);

  // Applies configuration with only one layer (layer_1).
  layer_t* test_layers_1[] = {&layer_1};
  display_config_t config = {
      .display_id = 1u,
      .mode = {},
      .cc_flags = 0u,
      .layer_list = test_layers_1,
      .layer_count = 1,
      .gamma_table_present = false,
      .apply_gamma_table = false,
  };
  config_stamp_t stamp_1 = {.value = 1u};
  pipe.ApplyConfiguration(&config, &stamp_1, GetGttImageHandle);

  // For images that are not registered with Pipe yet, GetVsyncConfigStamp()
  // should return nullopt.
  auto vsync_config_stamp_not_found = pipe.GetVsyncConfigStamp({kImageHandle2});
  EXPECT_FALSE(vsync_config_stamp_not_found.has_value());

  // Otherwise, for a valid image handle that has occurred in a past config,
  // GetVsyncConfigStamp() should return the latest config where it occurred.
  auto vsync_config_stamp_1 = pipe.GetVsyncConfigStamp({kImageHandle1});
  EXPECT_TRUE(vsync_config_stamp_1.has_value());
  EXPECT_EQ(vsync_config_stamp_1->value, stamp_1.value);

  // Applies another configuration with two layers (layer_2 replacing layer_1,
  // and a new layer layer_3).
  layer_t* test_layers_2[] = {&layer_2, &layer_3};
  display_config_t config_2 = {
      .display_id = 1u,
      .mode = {},
      .cc_flags = 0u,
      .layer_list = test_layers_2,
      .layer_count = 1,
      .gamma_table_present = false,
      .apply_gamma_table = false,
  };
  config_stamp_t stamp_2 = {.value = 2u};
  pipe.ApplyConfiguration(&config_2, &stamp_2, GetGttImageHandle);

  // It is possible that a layer update is slower than other layers, so on
  // Vsync time the device may have layers from different configurations. In
  // that case, the device should return the oldest configuration stamp, i.e.
  // stamp_1.
  auto vsync_config_stamp_2 = pipe.GetVsyncConfigStamp({kImageHandle1, kImageHandle3});
  EXPECT_TRUE(vsync_config_stamp_2.has_value());
  EXPECT_EQ(vsync_config_stamp_2->value, stamp_1.value);

  // Now both layers are updated in another new Vsync. GetVsyncConfigStamp()
  // should return the updated stamp value.
  auto vsync_config_stamp_3 = pipe.GetVsyncConfigStamp({kImageHandle2, kImageHandle3});
  EXPECT_TRUE(vsync_config_stamp_3.has_value());
  EXPECT_EQ(vsync_config_stamp_3->value, stamp_2.value);

  // Old image handle should be evicted from Pipe completely.
  auto vsync_config_stamp_4 = pipe.GetVsyncConfigStamp({kImageHandle1, kImageHandle3});
  EXPECT_FALSE(vsync_config_stamp_4.has_value());
}

}  // namespace i915
