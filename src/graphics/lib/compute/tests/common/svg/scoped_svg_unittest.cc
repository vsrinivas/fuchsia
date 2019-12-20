// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "scoped_svg.h"

#include <gtest/gtest.h>

TEST(ScopedSvg, Creation)
{
  static const char kSvgData[] = "<svg xmlns=\"http://www.w3.org/2000/svg\">\n"
                                 "  <g style = \"fill: #FF0000\">\n"
                                 "    <polyline points = \"0,0 16,0 16,16 0,16 0,0\"/>\n"
                                 "  </g>\n"
                                 "</svg>\n";

  ScopedSvg svg = ScopedSvg::parseString(kSvgData);
  ASSERT_TRUE(svg.get());
  ASSERT_EQ(1u, svg.path_count());
  ASSERT_EQ(1u, svg.raster_count());
  ASSERT_EQ(1u, svg.layer_count());
}
