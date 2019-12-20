// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "svg_bounds.h"

#include <gtest/gtest.h>

#include "scoped_svg.h"

TEST(svg_bounds, Rect)
{
  // clang-format off
  ScopedSvg svg = ScopedSvg::parseString(
    "<svg xmlns=\"http://www.w3.org/2000/svg\">"
    "  <g style = \"fill: #FF0000\">"
    "    <rect x=\"10\" y=\"20\" width=\"100\" height=\"50\" />"
    "  </g>"
    "</svg>"
  );
  // clang-format on

  double xmin, ymin, xmax, ymax;

  svg_estimate_bounds(svg.get(), nullptr, &xmin, &ymin, &xmax, &ymax);

  EXPECT_DOUBLE_EQ(xmin, 10.);
  EXPECT_DOUBLE_EQ(ymin, 20.);
  EXPECT_DOUBLE_EQ(xmax, 110.);
  EXPECT_DOUBLE_EQ(ymax, 70.);
}

TEST(svg_bounds, Path)
{
  // clang-format off
  ScopedSvg svg = ScopedSvg::parseString(
    "<svg xmlns=\"http://www.w3.org/2000/svg\">"
    "  <g style = \"fill: #FF0000\">"
    "    <path d=\"M 100 100 L 300 100 L 200 300 z\" />"
    "  </g>"
    "</svg>"
  );
  // clang-format on

  double xmin, ymin, xmax, ymax;

  svg_estimate_bounds(svg.get(), nullptr, &xmin, &ymin, &xmax, &ymax);

  EXPECT_DOUBLE_EQ(xmin, 100.);
  EXPECT_DOUBLE_EQ(ymin, 100.);
  EXPECT_DOUBLE_EQ(xmax, 300.);
  EXPECT_DOUBLE_EQ(ymax, 300.);
}
