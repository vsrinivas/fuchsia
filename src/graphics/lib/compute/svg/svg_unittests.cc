// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "svg.h"

namespace {

//
// SUCCESS
//

TEST(svg, svg_parse_success)
{
  char const doc[] = {

    "<svg xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <g style = \"fill: #FF0000\">\n"
    "    <polyline points = \"0,0 16,0 16,16 0,16 0,0\"/>\n"
    "  </g>\n"
    "</svg>\n"
  };

  struct svg * svg = svg_parse(doc, false);

  EXPECT_TRUE(svg != NULL) << svg;

  EXPECT_TRUE(svg_path_count(svg) == 1) << svg_path_count(svg);

  EXPECT_TRUE(svg_raster_count(svg) == 1) << svg_raster_count(svg);

  EXPECT_TRUE(svg_layer_count(svg) == 1) << svg_layer_count(svg);

  svg_dispose(svg);
}

//
// FAILURE: missing element
//

TEST(svg, svg_parse_failure_missing_element)
{
  char const doc[] = {

    "<svg xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <g style = \"fill: #FF0000\">\n"
    "    <INVALID points = \"0,0 16,0 16,16 0,16 0,0\"/>\n"
    "  </g>\n"
    "</svg>\n"
  };

  struct svg * svg = svg_parse(doc, false);

  EXPECT_TRUE(svg == NULL) << svg;
}

//
// FAILURE: invalid number
//

TEST(svg, svg_parse_failure_invalid_number)
{
  char const doc[] = {

    "<svg xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <g style = \"fill: #FF0000\">\n"
    "    <polyline points = \"INVALID,0 16,0 16,16 0,16 0,0\"/>\n"
    "  </g>\n"
    "</svg>\n"
  };

  struct svg * svg = svg_parse(doc, false);

  EXPECT_TRUE(svg == NULL) << svg;
}

//
// FAILURE: document not closed
//

TEST(svg, svg_parse_failure_not_closed)
{
  char const doc[] = {

    "<svg xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <g style = \"fill: #FF0000\">\n"
    "    <polyline points = \"0,0 16,0 16,16 0,16 0,0\"/>\n"
    "  </g>\n"
    // INVALID
  };

  struct svg * svg = svg_parse(doc, false);

  EXPECT_TRUE(svg == NULL) << svg;
}

//
// FAILURE: unrecognized color name
//

TEST(svg, svg_parse_failure_color_name)
{
  char const doc[] = {

    "<svg xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <g style = \"fill: INVALID\">\n"
    "    <polyline points = \"0,0 16,0 16,16 0,16 0,0\"/>\n"
    "  </g>\n"
    "</svg>\n"
  };

  struct svg * svg = svg_parse(doc, false);

  EXPECT_TRUE(svg == NULL) << svg;
}

//
// SUCCESS: parse an arc with concatenated flags
//

TEST(svg, svg_parse_success_arc_flags)
{
  char const doc[] = {
    // all four cases of flags: 00, 01, 10, 11
    "<svg xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <!-- no whitespace before flags -->\n"
    "  <path d=\"M1,1A1,1 45 001,1\"/>"
    "  <path d=\"M1,1A1,1 45 011,1\"/>"
    "  <path d=\"M1,1A1,1 45 101,1\"/>"
    "  <path d=\"M1,1A1,1 45 111,1\"/>"
    "  <!-- leading whitespace before flags -->\n"
    "  <path d=\"M1,1A1,1 45, 0, 0, 1,1\"/>"
    "  <path d=\"M1,1A1,1 45, 0, 1, 1,1\"/>"
    "  <path d=\"M1,1A1,1 45, 1, 0, 1,1\"/>"
    "  <path d=\"M1,1A1,1 45, 1, 1, 1,1\"/>"
    "</svg>\n"
  };

  struct svg * svg = svg_parse(doc, false);

  EXPECT_TRUE(svg != NULL) << svg;

  EXPECT_TRUE(svg_path_count(svg) == 8) << svg_path_count(svg);

  svg_dispose(svg);
}

}  // namespace
