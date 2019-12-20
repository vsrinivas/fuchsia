// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "svg_print.h"

#include <gtest/gtest.h>

#include <sstream>

#include "scoped_svg.h"

// TODO(digit): Move all these tests to data files in a directory.

// clang-format off

TEST(svg_print,Rect) {
  ScopedSvg svg = ScopedSvg::parseString(
    "<svg xmlns=\"http://www.w3.org/2000/svg\">"
    "  <g style = \"fill: #FF0000\">"
    "    <rect x=\"10\" y=\"20\" width=\"100\" height=\"50\" />"
    "  </g>"
    "</svg>"
  );

  std::stringstream ss;
  ss << svg.get();

  const char kExpected[] =
    "SVG Document (paths=1,rasters=1,layers=1) {\n"
    "  path[0]: Rect(x:10,y:20,w:100,h:50)\n"
    "  raster[0]: Fill(path:0)\n"
    "  layer[0]: FillColor(r:255,g:0,b:0),Place(raster:0,tx:0,ty:0)\n"
    "}\n";

  EXPECT_EQ(ss.str(), kExpected);
}

TEST(svg_print,Path) {
  // clang-format off
  ScopedSvg svg = ScopedSvg::parseString(
    "<svg xmlns=\"http://www.w3.org/2000/svg\">"
    "  <g style = \"fill: #FF0000\">"
    "    <path d=\"M 100 100 L 300 100 L 200 300 z\" />"
    "  </g>"
    "</svg>"
  );
  // clang-format on

  std::stringstream ss;
  ss << svg.get();
  const char kExpected[] =
    "SVG Document (paths=1,rasters=1,layers=1) {\n"
    "  path[0]: Path(MoveTo(x:100,y:100),LineTo(x:300,y:100),LineTo(x:200,y:300),Close)\n"
    "  raster[0]: Fill(path:0)\n"
    "  layer[0]: FillColor(r:255,g:0,b:0),Place(raster:0,tx:0,ty:0)\n"
    "}\n";

  EXPECT_EQ(ss.str(), kExpected);
}

// clang-format on
