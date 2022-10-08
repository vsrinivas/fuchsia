// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/registers-pipe.h"

#include <gtest/gtest.h>
#include <hwreg/bitfields.h>

namespace i915_tgl {

namespace {

TEST(PlaneControlTest, PipeSliceRequestArbitrationSlotCountTigerLake) {
  auto plane_ctl_1_a = hwreg::RegisterAddr<tgl_registers::PlaneControl>(0x70280).FromValue(0);

  plane_ctl_1_a.set_reg_value(0).set_pipe_slice_request_arbitration_slot_count_tiger_lake(8);
  EXPECT_EQ(0b0111'0000'0000'0000'0000'0000'0000'0000u, plane_ctl_1_a.reg_value());
  EXPECT_EQ(8, plane_ctl_1_a.pipe_slice_request_arbitration_slot_count_tiger_lake());

  plane_ctl_1_a.set_reg_value(0).set_pipe_slice_request_arbitration_slot_count_tiger_lake(7);
  EXPECT_EQ(0b0110'0000'0000'0000'0000'0000'0000'0000u, plane_ctl_1_a.reg_value());
  EXPECT_EQ(7, plane_ctl_1_a.pipe_slice_request_arbitration_slot_count_tiger_lake());

  plane_ctl_1_a.set_reg_value(0).set_pipe_slice_request_arbitration_slot_count_tiger_lake(5);
  EXPECT_EQ(0b0100'0000'0000'0000'0000'0000'0000'0000u, plane_ctl_1_a.reg_value());
  EXPECT_EQ(5, plane_ctl_1_a.pipe_slice_request_arbitration_slot_count_tiger_lake());

  plane_ctl_1_a.set_reg_value(0xffff'ffff)
      .set_pipe_slice_request_arbitration_slot_count_tiger_lake(1);
  EXPECT_EQ(0b1000'1111'1111'1111'1111'1111'1111'1111u, plane_ctl_1_a.reg_value());
  EXPECT_EQ(1, plane_ctl_1_a.pipe_slice_request_arbitration_slot_count_tiger_lake());
}

TEST(PlaneControlTest, HasYComponentInPlanarYuv420TigerLake) {
  auto plane_ctl_1_a = hwreg::RegisterAddr<tgl_registers::PlaneControl>(0x70280).FromValue(0);

  plane_ctl_1_a.set_reg_value(0).set_has_y_component_in_planar_yuv420_tiger_lake(true);
  EXPECT_EQ(0b0000'0000'0000'1000'0000'0000'0000'0000u, plane_ctl_1_a.reg_value());
  EXPECT_EQ(true, plane_ctl_1_a.has_y_component_in_planar_yuv420_tiger_lake());

  plane_ctl_1_a.set_reg_value(0xffff'ffff).set_has_y_component_in_planar_yuv420_tiger_lake(false);
  EXPECT_EQ(0b1111'1111'1111'0111'1111'1111'1111'1111u, plane_ctl_1_a.reg_value());
  EXPECT_EQ(false, plane_ctl_1_a.has_y_component_in_planar_yuv420_tiger_lake());
}

TEST(PlaneControlTest, RenderDecompressionClearColorDisabledTigerLake) {
  auto plane_ctl_1_a = hwreg::RegisterAddr<tgl_registers::PlaneControl>(0x70280).FromValue(0);

  plane_ctl_1_a.set_reg_value(0).set_render_decompression_clear_color_disabled_tiger_lake(true);
  EXPECT_EQ(0b0000'0000'0000'0000'0010'0000'0000'0000u, plane_ctl_1_a.reg_value());
  EXPECT_EQ(true, plane_ctl_1_a.render_decompression_clear_color_disabled_tiger_lake());

  plane_ctl_1_a.set_reg_value(0xffff'ffff)
      .set_render_decompression_clear_color_disabled_tiger_lake(false);
  EXPECT_EQ(0b1111'1111'1111'1111'1101'1111'1111'1111u, plane_ctl_1_a.reg_value());
  EXPECT_EQ(false, plane_ctl_1_a.render_decompression_clear_color_disabled_tiger_lake());
}

TEST(PlaneControlTest, DecompressMediaCompressedSurfacesTigerLake) {
  auto plane_ctl_1_a = hwreg::RegisterAddr<tgl_registers::PlaneControl>(0x70280).FromValue(0);

  plane_ctl_1_a.set_reg_value(0).set_decompress_media_compressed_surfaces_tiger_lake(true);
  EXPECT_EQ(0b0000'0000'0000'0000'0000'0000'0001'0000u, plane_ctl_1_a.reg_value());
  EXPECT_EQ(true, plane_ctl_1_a.decompress_media_compressed_surfaces_tiger_lake());

  plane_ctl_1_a.set_reg_value(0xffff'ffff)
      .set_decompress_media_compressed_surfaces_tiger_lake(false);
  EXPECT_EQ(0b1111'1111'1111'1111'1111'1111'1110'1111u, plane_ctl_1_a.reg_value());
  EXPECT_EQ(false, plane_ctl_1_a.decompress_media_compressed_surfaces_tiger_lake());
}

}  // namespace

}  // namespace i915_tgl
