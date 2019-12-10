// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spinel_test_utils.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <climits>
#include <sstream>

#include "spinel/spinel_opcodes.h"

TEST(SpinelTestUtils, Printers)
{
  std::stringstream ss;

  ss.str("");
  ss << spn_path_t{ UINT32_MAX };
  EXPECT_EQ(ss.str(), "SpnPath[INVALID]");

  ss.str("");
  ss << spn_path_t{ 42u };
  EXPECT_EQ(ss.str(), "SpnPath[42]");

  ss.str("");
  ss << spn_raster_t{ 32u };
  EXPECT_EQ(ss.str(), "SpnRaster[32]");

  ss.str("");
  ss << spn_raster_t{ UINT32_MAX };
  EXPECT_EQ(ss.str(), "SpnRaster[INVALID]");
}

TEST(SpinelTestUtils, AssertionMacros)
{
  spn_transform_t t = { .sx = 1., .sy = 1. };

  EXPECT_SPN_TRANSFORM_EQ(t, spinel_constants::identity_transform);
  EXPECT_SPN_TRANSFORM_IS_IDENTITY(t);

  spn_clip_t c = { 0., 0., FLT_MAX, FLT_MAX };
  EXPECT_SPN_CLIP_EQ(c, spinel_constants::default_clip);

  spn_txty_t tt = { 0, 0 };
  EXPECT_SPN_TXTY_EQ(tt, spinel_constants::default_txty);
}

TEST(SpinelTestUtils, StylingCommandsToString)
{
  EXPECT_EQ("NOOP",
            spinelStylingCommandsToString({
              SPN_STYLING_OPCODE_NOOP,
            }));
  EXPECT_EQ("COVER_WIP_ZERO,COLOR_ACC_ZERO",
            spinelStylingCommandsToString({
              SPN_STYLING_OPCODE_COVER_WIP_ZERO,
              SPN_STYLING_OPCODE_COLOR_ACC_ZERO,
            }));
}

TEST(SpinelTestUtils, DefaultValues)
{
  EXPECT_FLOAT_EQ(spinel_constants::default_clip.x0, 0.);
  EXPECT_FLOAT_EQ(spinel_constants::default_clip.y0, 0.);
  EXPECT_FLOAT_EQ(spinel_constants::default_clip.x1, FLT_MAX);
  EXPECT_FLOAT_EQ(spinel_constants::default_clip.y1, FLT_MAX);

  EXPECT_EQ(spinel_constants::default_txty.tx, 0);
  EXPECT_EQ(spinel_constants::default_txty.ty, 0);

  EXPECT_FLOAT_EQ(spinel_constants::identity_transform.sx, 1.);
  EXPECT_FLOAT_EQ(spinel_constants::identity_transform.shx, 0.);
  EXPECT_FLOAT_EQ(spinel_constants::identity_transform.tx, 0.);
  EXPECT_FLOAT_EQ(spinel_constants::identity_transform.shy, 0.);
  EXPECT_FLOAT_EQ(spinel_constants::identity_transform.sy, 1.);
  EXPECT_FLOAT_EQ(spinel_constants::identity_transform.ty, 0.);
  EXPECT_FLOAT_EQ(spinel_constants::identity_transform.w0, 0.);
  EXPECT_FLOAT_EQ(spinel_constants::identity_transform.w1, 0.);
}
