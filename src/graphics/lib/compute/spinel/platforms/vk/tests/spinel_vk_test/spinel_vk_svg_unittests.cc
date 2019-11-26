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
using param         = param_spinel_vk_render;

//
//
//
TEST_P(spinel_vk_svg, svg_tests)
{
  ;
}

//
// Each test is a name, surface size, a snippet of SVG and a device-specific checksum
//
param const params[] = {
  {
    .name    = "black_square_2x2",
    .surface = { 1024, 1024 },
    .svg     =  //
    "<svg xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <g style = \"fill: black\">\n"
    "    <polyline points = \"2,2 4,2 4,4 2,4 2,2\"/>\n"
    "  </g>\n"
    "</svg>",      //
    .checksums = { //
      { 0xFBF00004, {} }
    }
  },
  {
    .name    = "red_square_2x2",
    .surface = { 1024, 1024 },
    .svg     =  //
    "<svg xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <g style = \"fill: red\">\n"
    "    <polyline points = \"2,2 4,2 4,4 2,4 2,2\"/>\n"
    "  </g>\n"
    "</svg>",      //
    .checksums = { //
      { 0xFBF00400, {} }
    }
  },
  {
    .name    = "rasters_prefix_fix",
    .surface = { 1024, 300 },
    .svg     =  //
    "<svg xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <g fill=\"black\"\n"
    "     transform=\"translate(-900,-950)\n"
    "                 scale(0.03125)\n"
    "                 matrix(-63986.14, -1331.7272, 1331.7272, -63986.14, 48960.0, 33920.0)\">\n"
    "    <polyline points =\n"
    "              \"-0.08,-0.02 0.28,-0.02 0.28,-0.02 0.28,0.02\n"
    "               0.28,0.02 -0.08,0.02 -0.08,0.02 -0.08,-0.02\"/>\n"
    "  </g>\n"
    "</svg>\n",
    // Checksum varies due to differing fp32 and imageStore() implementations
    .checksums = {
      { 0xFD0B4012, {
          { param::INTEL,  {}                    },  // all intel
          { param::AMD,    { param::AMD_V1807B } } } // AMD/V1807B (Mesa)
      },
      { 0xFCF529FC, {
          { param::NVIDIA, {}                    } } // all nvidia
      },
    }
  }
};

//
//
//
INSTANTIATE_TEST_SUITE_P(spinel_vk_svg_tests,  //
                         spinel_vk_svg,        //
                         ::testing::ValuesIn(params),
                         fxt_spinel_vk_render::param_name);

}  // namespace spinel::vk::test

//
//
//
