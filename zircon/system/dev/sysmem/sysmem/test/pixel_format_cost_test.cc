// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/platform-defs.h>
#include <zxtest/zxtest.h>

#include "usage_pixel_format_cost.h"

namespace {
TEST(PixelFormatCost, Afbc) {
    fuchsia_sysmem_BufferCollectionConstraints constraints = {};
    constraints.image_format_constraints_count = 2;
    constraints.image_format_constraints[0].pixel_format.type =
        fuchsia_sysmem_PixelFormatType_BGRA32;
    constraints.image_format_constraints[0].pixel_format.has_format_modifier = false;
    constraints.image_format_constraints[1].pixel_format.type =
        fuchsia_sysmem_PixelFormatType_BGRA32;
    constraints.image_format_constraints[1].pixel_format.has_format_modifier = true;
    constraints.image_format_constraints[1].pixel_format.format_modifier.value =
        fuchsia_sysmem_FORMAT_MODIFIER_ARM_AFBC_32x8;

    EXPECT_LT(0, UsagePixelFormatCost::Compare(PDEV_VID_AMLOGIC, PDEV_PID_AMLOGIC_S912,
                                               &constraints, 0, 1));
    EXPECT_GT(0, UsagePixelFormatCost::Compare(PDEV_VID_AMLOGIC, PDEV_PID_AMLOGIC_S912,
                                               &constraints, 1, 0));
    EXPECT_EQ(0, UsagePixelFormatCost::Compare(0u, PDEV_PID_AMLOGIC_S912, &constraints, 0, 1));
    EXPECT_EQ(0, UsagePixelFormatCost::Compare(0u, PDEV_PID_AMLOGIC_S912, &constraints, 1, 0));
}
}
