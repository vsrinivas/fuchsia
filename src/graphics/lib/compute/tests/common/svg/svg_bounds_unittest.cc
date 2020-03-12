// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "svg_bounds.h"

#include <gtest/gtest.h>

#include "scoped_svg.h"

static const double kEpsilon = 1e-11;

#define EXPECT_DOUBLE_NEAR(a, b) EXPECT_NEAR(a, b, kEpsilon)

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

  EXPECT_DOUBLE_NEAR(xmin, 10.);
  EXPECT_DOUBLE_NEAR(ymin, 20.);
  EXPECT_DOUBLE_NEAR(xmax, 110.);
  EXPECT_DOUBLE_NEAR(ymax, 70.);
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

  EXPECT_DOUBLE_NEAR(xmin, 100.);
  EXPECT_DOUBLE_NEAR(ymin, 100.);
  EXPECT_DOUBLE_NEAR(xmax, 300.);
  EXPECT_DOUBLE_NEAR(ymax, 300.);
}

TEST(svg_bounds, TransformedPath)
{
  // clang-format off
  ScopedSvg svg = ScopedSvg::parseString(
    "<svg xmlns=\"http://www.w3.org/2000/svg\">"
    "  <g style = \"fill: #FF0000\" transform=\"translate(100,0) rotate(90)\">"
    "    <path d=\"M 100 100 L 300 100 L 200 300 z\" />"
    "  </g>"
    "</svg>"
  );
  // clang-format on

  double xmin, ymin, xmax, ymax;

  svg_estimate_bounds(svg.get(), nullptr, &xmin, &ymin, &xmax, &ymax);

  EXPECT_DOUBLE_NEAR(xmin, -200.);
  EXPECT_DOUBLE_NEAR(ymin, 100.);
  EXPECT_DOUBLE_NEAR(xmax, 0.);
  EXPECT_DOUBLE_NEAR(ymax, 300.);
}
