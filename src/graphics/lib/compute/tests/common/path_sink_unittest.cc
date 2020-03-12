// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tests/common/path_sink.h"

#include <gtest/gtest.h>

#include <cmath>

#include "tests/common/path_sink_test_utils.h"

TEST(PathSink, TestConvenienceMethods)
{
  RecordingPathSink sink;

  sink.begin();
  sink.addMoveTo(1., 2.);
  sink.addLineTo(3., 4.);
  sink.addQuadTo(5., 6., 7., 8.);
  sink.addCubicTo(9., 10., 11., 12., 13., 14.);
  sink.addRatQuadTo(15., 16., 17., 18., 0.1);
  sink.addRatCubicTo(19., 20., 21., 22., 23., 24., 0.2, 0.3);
  EXPECT_TRUE(sink.end());

  ASSERT_EQ(sink.commands.size(), 8u);

  // clang-format off
  static const char kExpected[] =
      "BEGIN;"
      "MOVE_TO(1 2);"
      "LINE_TO(3 4);"
      "QUAD_TO(5 6 7 8);"
      "CUBIC_TO(9 10 11 12 13 14);"
      "RAT_QUAD_TO(15 16 17 18 0.1);"
      "RAT_CUBIC_TO(19 20 21 22 23 24 0.2 0.3);"
      "END";
  // clang-format on

  EXPECT_STREQ(sink.to_string().c_str(), kExpected);
}

TEST(PathSink, TestArcTo)
{
  RecordingPathSink sink;

  sink.begin();
  sink.addMoveTo(1., 0.);
  sink.addArcTo(0., 0., 1., 1., 0., 0., M_PI * 2);  // Full unit circle.
  EXPECT_TRUE(sink.end());

  // clang-format off
  static const char kExpected[] =
    "BEGIN;MOVE_TO(1 0);"
    "RAT_QUAD_TO(1 1 0 1 0.707107);"
    "RAT_QUAD_TO(-1 1 -1 0 0.707107);"
    "RAT_QUAD_TO(-1 -1 0 -1 0.707107);"
    "RAT_QUAD_TO(1 -1 1 0 0.707107);"
    "END";
  // clang-format on

  EXPECT_STREQ(sink.to_string().c_str(), kExpected);
}

TEST(PathSink, TestSvgArcTo)
{
  RecordingPathSink sink;

  static const double dx = sqrt(2) / 2.;

  sink.begin();
  sink.addMoveTo(1., 0.);
  sink.addSvgArcTo(1., 0., -dx, -dx, 1., 1., 0., true, true);
  EXPECT_TRUE(sink.end());

  // clang-format off
  static const char kExpected[] =
    "BEGIN;MOVE_TO(1 0);"
    "RAT_QUAD_TO(1 1 0 1 0.707107);"
    "RAT_QUAD_TO(-1 1 -1 0 0.707107);"
    "RAT_QUAD_TO(-1 -0.414214 -0.707107 -0.707107 0.92388);"
    "END";
  // clang-format on

  EXPECT_STREQ(sink.to_string().c_str(), kExpected);
}
