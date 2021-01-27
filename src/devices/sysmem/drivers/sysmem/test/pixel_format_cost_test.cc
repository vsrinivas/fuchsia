// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sysmem2/llcpp/fidl.h>
#include <lib/fidl/llcpp/heap_allocator.h>
#include <lib/sysmem-make-tracking/make_tracking.h>

#include <ddk/platform-defs.h>
#include <zxtest/zxtest.h>

#include "usage_pixel_format_cost.h"

namespace sysmem_driver {
namespace {

fidl::HeapAllocator heap_allocator;

TEST(PixelFormatCost, Afbc) {
  auto constraints =
      heap_allocator.make_table_builder<llcpp::fuchsia::sysmem2::BufferCollectionConstraints>();
  constraints.set_image_format_constraints(
      heap_allocator.make_vec_ptr<llcpp::fuchsia::sysmem2::ImageFormatConstraints>(2));
  constraints.image_format_constraints()[0] =
      heap_allocator.make_table_builder<llcpp::fuchsia::sysmem2::ImageFormatConstraints>()
          .set_pixel_format(sysmem::MakeTracking(
              &heap_allocator,
              heap_allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
                  .set_type(sysmem::MakeTracking(&heap_allocator,
                                                 llcpp::fuchsia::sysmem2::PixelFormatType::BGRA32))
                  .build()))
          .build();
  constraints.image_format_constraints()[1] =
      heap_allocator.make_table_builder<llcpp::fuchsia::sysmem2::ImageFormatConstraints>()
          .set_pixel_format(sysmem::MakeTracking(
              &heap_allocator,
              heap_allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
                  .set_type(sysmem::MakeTracking(&heap_allocator,
                                                 llcpp::fuchsia::sysmem2::PixelFormatType::BGRA32))
                  .set_format_modifier_value(sysmem::MakeTracking(
                      &heap_allocator, llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_AFBC_32X8))
                  .build()))
          .build();

  uint32_t amlogic_pids[] = {
      PDEV_PID_AMLOGIC_S912,
      PDEV_PID_AMLOGIC_S905D2,
      PDEV_PID_AMLOGIC_T931,
      PDEV_PID_AMLOGIC_A311D,
  };
  for (uint32_t pid : amlogic_pids) {
    EXPECT_LT(0, UsagePixelFormatCost::Compare(PDEV_VID_AMLOGIC, pid, constraints, 0, 1));
    EXPECT_GT(0, UsagePixelFormatCost::Compare(PDEV_VID_AMLOGIC, pid, constraints, 1, 0));
  }
  EXPECT_EQ(0, UsagePixelFormatCost::Compare(0u, PDEV_PID_AMLOGIC_S912, constraints, 0, 1));
  EXPECT_EQ(0, UsagePixelFormatCost::Compare(0u, PDEV_PID_AMLOGIC_S912, constraints, 1, 0));
}

TEST(PixelFormatCost, IntelTiling) {
  auto constraints =
      heap_allocator.make_table_builder<llcpp::fuchsia::sysmem2::BufferCollectionConstraints>();
  constraints.set_image_format_constraints(
      heap_allocator.make_vec_ptr<llcpp::fuchsia::sysmem2::ImageFormatConstraints>(2));
  uint64_t tiling_types[] = {llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_INTEL_I915_X_TILED,
                             llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_INTEL_I915_YF_TILED,
                             llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_INTEL_I915_Y_TILED};
  for (auto modifier : tiling_types) {
    constraints.image_format_constraints()[0] =
        heap_allocator.make_table_builder<llcpp::fuchsia::sysmem2::ImageFormatConstraints>()
            .set_pixel_format(sysmem::MakeTracking(
                &heap_allocator,
                heap_allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
                    .set_type(sysmem::MakeTracking(
                        &heap_allocator, llcpp::fuchsia::sysmem2::PixelFormatType::BGRA32))
                    .set_format_modifier_value(sysmem::MakeTracking(
                        &heap_allocator, llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_LINEAR))
                    .build()))
            .build();
    constraints.image_format_constraints()[1] =
        heap_allocator.make_table_builder<llcpp::fuchsia::sysmem2::ImageFormatConstraints>()
            .set_pixel_format(sysmem::MakeTracking(
                &heap_allocator,
                heap_allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
                    .set_type(sysmem::MakeTracking(
                        &heap_allocator, llcpp::fuchsia::sysmem2::PixelFormatType::BGRA32))
                    .set_format_modifier_value(sysmem::MakeTracking(&heap_allocator, modifier))
                    .build()))
            .build();
    constexpr uint32_t kUnknownPid = 0;
    constexpr uint32_t kUnknownVid = 0;
    EXPECT_LT(0, UsagePixelFormatCost::Compare(kUnknownVid, kUnknownPid, constraints, 0, 1));
    EXPECT_GT(0, UsagePixelFormatCost::Compare(kUnknownVid, kUnknownPid, constraints, 1, 0));
    // Intel tiled formats aren't necessarily useful on AMLOGIC, but if some hardware supported them
    // they should probably be used anyway.
    EXPECT_LT(0, UsagePixelFormatCost::Compare(PDEV_VID_AMLOGIC, PDEV_PID_AMLOGIC_S912, constraints,
                                               0, 1));
    EXPECT_GT(0, UsagePixelFormatCost::Compare(PDEV_VID_AMLOGIC, PDEV_PID_AMLOGIC_S912, constraints,
                                               1, 0));

    // Explicit linear should be treated the same as no format modifier value.
    constraints.image_format_constraints()[0].pixel_format().format_modifier_value() =
        llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_NONE;
    EXPECT_LT(0, UsagePixelFormatCost::Compare(kUnknownVid, kUnknownPid, constraints, 0, 1));
    EXPECT_GT(0, UsagePixelFormatCost::Compare(kUnknownVid, kUnknownPid, constraints, 1, 0));

    // Explicit linear should be treated the same as no format modifier value.
    constraints.image_format_constraints()[0].pixel_format() =
        heap_allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
            .set_type(sysmem::MakeTracking(&heap_allocator,
                                           llcpp::fuchsia::sysmem2::PixelFormatType::BGRA32))
            .build();
    EXPECT_LT(0, UsagePixelFormatCost::Compare(kUnknownVid, kUnknownPid, constraints, 0, 1));
    EXPECT_GT(0, UsagePixelFormatCost::Compare(kUnknownVid, kUnknownPid, constraints, 1, 0));
  }
}

TEST(PixelFormatCost, ArmTransactionElimination) {
  auto constraints =
      heap_allocator.make_table_builder<llcpp::fuchsia::sysmem2::BufferCollectionConstraints>();
  constraints.set_image_format_constraints(
      heap_allocator.make_vec_ptr<llcpp::fuchsia::sysmem2::ImageFormatConstraints>(2));
  constraints.image_format_constraints()[0] =
      heap_allocator.make_table_builder<llcpp::fuchsia::sysmem2::ImageFormatConstraints>()
          .set_pixel_format(sysmem::MakeTracking(
              &heap_allocator,
              heap_allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
                  .set_type(sysmem::MakeTracking(&heap_allocator,
                                                 llcpp::fuchsia::sysmem2::PixelFormatType::BGRA32))
                  .set_format_modifier_value(sysmem::MakeTracking(
                      &heap_allocator, llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_AFBC_32X8))
                  .build()))
          .build();
  constraints.image_format_constraints()[1] =
      heap_allocator.make_table_builder<llcpp::fuchsia::sysmem2::ImageFormatConstraints>()
          .set_pixel_format(sysmem::MakeTracking(
              &heap_allocator,
              heap_allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
                  .set_type(sysmem::MakeTracking(&heap_allocator,
                                                 llcpp::fuchsia::sysmem2::PixelFormatType::BGRA32))
                  .set_format_modifier_value(sysmem::MakeTracking(
                      &heap_allocator, llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_AFBC_32X8_TE))
                  .build()))
          .build();

  EXPECT_LT(
      0, UsagePixelFormatCost::Compare(PDEV_VID_AMLOGIC, PDEV_PID_AMLOGIC_S912, constraints, 0, 1));
  EXPECT_GT(
      0, UsagePixelFormatCost::Compare(PDEV_VID_AMLOGIC, PDEV_PID_AMLOGIC_S912, constraints, 1, 0));
  EXPECT_EQ(0, UsagePixelFormatCost::Compare(0u, PDEV_PID_AMLOGIC_S912, constraints, 0, 1));
  EXPECT_EQ(0, UsagePixelFormatCost::Compare(0u, PDEV_PID_AMLOGIC_S912, constraints, 1, 0));
}

TEST(PixelFormatCost, AfbcWithFlags) {
  // Formats are in ascending preference order (descending cost order).
  std::array modifier_list = {
      llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_LINEAR,
      llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_AFBC_16X16,
      llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_AFBC_16X16_SPLIT_BLOCK_SPARSE_YUV,
      llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_AFBC_16X16_SPLIT_BLOCK_SPARSE_YUV_TILED_HEADER,
      llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_AFBC_16X16_TE,
      llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_AFBC_16X16_SPLIT_BLOCK_SPARSE_YUV_TE,
      llcpp::fuchsia::sysmem2::
          FORMAT_MODIFIER_ARM_AFBC_16X16_SPLIT_BLOCK_SPARSE_YUV_TE_TILED_HEADER,
  };
  auto constraints =
      heap_allocator.make_table_builder<llcpp::fuchsia::sysmem2::BufferCollectionConstraints>();
  constraints.set_image_format_constraints(
      heap_allocator.make_vec_ptr<llcpp::fuchsia::sysmem2::ImageFormatConstraints>(
          modifier_list.size()));

  for (uint32_t i = 0; i < modifier_list.size(); ++i) {
    constraints.image_format_constraints()[i] =
        heap_allocator.make_table_builder<llcpp::fuchsia::sysmem2::ImageFormatConstraints>()
            .set_pixel_format(sysmem::MakeTracking(
                &heap_allocator,
                heap_allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
                    .set_type(sysmem::MakeTracking(
                        &heap_allocator, llcpp::fuchsia::sysmem2::PixelFormatType::BGRA32))
                    .set_format_modifier_value(
                        sysmem::MakeTracking(&heap_allocator, modifier_list[i]))
                    .build()))
            .build();
  }

  for (uint32_t i = 1; i < modifier_list.size(); ++i) {
    EXPECT_LT(0,
              UsagePixelFormatCost::Compare(PDEV_VID_AMLOGIC, PDEV_PID_AMLOGIC_S912, constraints,
                                            i - 1, i),
              "i=%d", i);
    EXPECT_GT(0,
              UsagePixelFormatCost::Compare(PDEV_VID_AMLOGIC, PDEV_PID_AMLOGIC_S912, constraints, i,
                                            i - 1),
              "i=%d", i);
    EXPECT_EQ(0, UsagePixelFormatCost::Compare(0u, PDEV_PID_AMLOGIC_S912, constraints, i - 1, i));
    EXPECT_EQ(0, UsagePixelFormatCost::Compare(0u, PDEV_PID_AMLOGIC_S912, constraints, i, i - 1));
  }
}

}  // namespace
}  // namespace sysmem_driver
