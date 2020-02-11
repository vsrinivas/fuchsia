// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/platform-defs.h>
#include <zxtest/zxtest.h>

#include "usage_pixel_format_cost.h"

namespace sysmem_driver {
namespace {

TEST(PixelFormatCost, Afbc) {
  fuchsia_sysmem_BufferCollectionConstraints constraints = {};
  constraints.image_format_constraints_count = 2;
  constraints.image_format_constraints[0].pixel_format.type = fuchsia_sysmem_PixelFormatType_BGRA32;
  constraints.image_format_constraints[0].pixel_format.has_format_modifier = false;
  constraints.image_format_constraints[1].pixel_format.type = fuchsia_sysmem_PixelFormatType_BGRA32;
  constraints.image_format_constraints[1].pixel_format.has_format_modifier = true;
  constraints.image_format_constraints[1].pixel_format.format_modifier.value =
      fuchsia_sysmem_FORMAT_MODIFIER_ARM_AFBC_32x8;

  EXPECT_LT(0, UsagePixelFormatCost::Compare(PDEV_VID_AMLOGIC, PDEV_PID_AMLOGIC_S912, &constraints,
                                             0, 1));
  EXPECT_GT(0, UsagePixelFormatCost::Compare(PDEV_VID_AMLOGIC, PDEV_PID_AMLOGIC_S912, &constraints,
                                             1, 0));
  EXPECT_EQ(0, UsagePixelFormatCost::Compare(0u, PDEV_PID_AMLOGIC_S912, &constraints, 0, 1));
  EXPECT_EQ(0, UsagePixelFormatCost::Compare(0u, PDEV_PID_AMLOGIC_S912, &constraints, 1, 0));
}

TEST(PixelFormatCost, IntelTiling) {
  fuchsia_sysmem_BufferCollectionConstraints constraints = {};
  constraints.image_format_constraints_count = 2;
  constraints.image_format_constraints[0].pixel_format.type = fuchsia_sysmem_PixelFormatType_BGRA32;
  constraints.image_format_constraints[0].pixel_format.has_format_modifier = true;
  constraints.image_format_constraints[1].pixel_format.format_modifier.value =
      fuchsia_sysmem_FORMAT_MODIFIER_LINEAR;

  uint64_t tiling_types[] = {fuchsia_sysmem_FORMAT_MODIFIER_INTEL_I915_X_TILED,
                             fuchsia_sysmem_FORMAT_MODIFIER_INTEL_I915_YF_TILED,
                             fuchsia_sysmem_FORMAT_MODIFIER_INTEL_I915_Y_TILED};
  for (auto modifier : tiling_types) {
    constraints.image_format_constraints[0].pixel_format.has_format_modifier = true;
    constraints.image_format_constraints[1].pixel_format.type =
        fuchsia_sysmem_PixelFormatType_BGRA32;
    constraints.image_format_constraints[1].pixel_format.has_format_modifier = true;
    constraints.image_format_constraints[1].pixel_format.format_modifier.value = modifier;

    constexpr uint32_t kUnknownPid = 0;
    constexpr uint32_t kUnknownVid = 0;
    EXPECT_LT(0, UsagePixelFormatCost::Compare(kUnknownVid, kUnknownPid, &constraints, 0, 1));
    EXPECT_GT(0, UsagePixelFormatCost::Compare(kUnknownVid, kUnknownPid, &constraints, 1, 0));
    // Intel tiled formats aren't necessarily useful on AMLOGIC, but if some hardware supported them
    // they should probably be used anyway.
    EXPECT_LT(0, UsagePixelFormatCost::Compare(PDEV_VID_AMLOGIC, PDEV_PID_AMLOGIC_S912,
                                               &constraints, 0, 1));
    EXPECT_GT(0, UsagePixelFormatCost::Compare(PDEV_VID_AMLOGIC, PDEV_PID_AMLOGIC_S912,
                                               &constraints, 1, 0));

    // Explicit linear should be treated the same as no format modifier.
    constraints.image_format_constraints[0].pixel_format.has_format_modifier = false;
    EXPECT_LT(0, UsagePixelFormatCost::Compare(kUnknownVid, kUnknownPid, &constraints, 0, 1));
    EXPECT_GT(0, UsagePixelFormatCost::Compare(kUnknownVid, kUnknownPid, &constraints, 1, 0));
  }
}

}  // namespace
}  // namespace sysmem_driver
