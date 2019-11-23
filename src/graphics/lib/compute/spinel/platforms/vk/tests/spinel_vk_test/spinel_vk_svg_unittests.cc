// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fxt_spinel_vk_svg.h"

//
//
//

namespace spinel::vk::test {

// alias for test output aesthetics
using spinel_vk_svg = fxt_spinel_vk_svg;

//
//
//
TEST_P(spinel_vk_svg, svg_tests)
{
  ;
}

//
//
//
param_spinel_vk_render const params_tests[] = {
  {
    .name    = "black_square_2x2",
    .surface = { 1024, 1024 },
    .svg     =  //
    "<svg xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <g style = \"fill: black\">\n"
    "    <polyline points = \"2,2 4,2 4,4 2,4 2,2\"/>\n"
    "  </g>\n"
    "</svg>",  //
    .checksum = 0xFBF00004,
  },
  {
    .name    = "red_square_2x2",
    .surface = { 1024, 1024 },
    .svg     =  //
    "<svg xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <g style = \"fill: red\">\n"
    "    <polyline points = \"2,2 4,2 4,4 2,4 2,2\"/>\n"
    "  </g>\n"
    "</svg>",  //
    .checksum = 0xFBF00400,
  },
};

//
//
//

INSTANTIATE_TEST_SUITE_P(spinel_vk_svg_tests,  //
                         spinel_vk_svg,        //
                         ::testing::ValuesIn(params_tests),
                         fxt_spinel_vk_render::param_name);

}  // namespace spinel::vk::test

//
//
//
