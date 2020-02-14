// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fxt_spinel_vk_render.h"
#include "spinel/ext/svg2spinel/svg2spinel.h"
#include "spinel/ext/transform_stack/transform_stack.h"
#include "svg/svg.h"

//
//
//

namespace spinel::vk::test {

//
// SVG tests
//

struct test_spinel_vk_svg : test_spinel_vk_render
{
  char const * svg_string;
  struct svg * svg;

  spn_path_t *   paths;
  spn_raster_t * rasters;

  test_spinel_vk_svg(char const * svg_string) : svg_string(svg_string)
  {
    ;
  }

  void
  create()
  {
    svg = svg_parse(svg_string, false);

    ASSERT_NE(svg, nullptr);
  }

  void
  dispose()
  {
    svg_dispose(svg);
  }

  uint32_t
  layer_count()
  {
    return svg_layer_count(svg);
  }

  void
  paths_create(spn_path_builder_t pb)
  {
    paths = spn_svg_paths_decode(svg, pb);
  }

  void
  rasters_create(spn_raster_builder_t rb, struct transform_stack * const ts)
  {
    rasters = spn_svg_rasters_decode(svg, rb, paths, ts);
  }

  void
  layers_create(spn_composition_t composition, spn_styling_t styling, bool is_srgb)
  {
    spn_svg_layers_decode(svg, rasters, composition, styling, is_srgb);
  }

  void
  paths_dispose(spn_context_t context)
  {
    spn_svg_paths_release(svg, context, paths);
  }

  void
  rasters_dispose(spn_context_t context)
  {
    spn_svg_rasters_release(svg, context, rasters);
  }
};

//
// alias for test output aesthetics
//
using spinel_vk_svg = fxt_spinel_vk_render;
using param         = param_spinel_vk_render;
using test          = test_spinel_vk_svg;

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
      .name      = "black_square_2x2",
      .surface   = { 1024, 1024 },
      .checksums = {
        { 0xFBF00004, {} }
      },
      .test = std::make_shared<test>(
        "<svg xmlns=\"http://www.w3.org/2000/svg\">\n"
        "  <g style = \"fill: black\">\n"
        "    <polyline points = \"2,2 4,2 4,4 2,4 2,2\"/>\n"
        "  </g>\n"
        "</svg>")
    },
    {
      .name      = "red_square_2x2",
      .surface   = { 1024, 1024 },
      .checksums = {
        { 0xFBF00400, {} }
      },
      .test = std::make_shared<test>(
        "<svg xmlns=\"http://www.w3.org/2000/svg\">\n"
        "  <g style = \"fill: red\">\n"
        "    <polyline points = \"2,2 4,2 4,4 2,4 2,2\"/>\n"
        "  </g>\n"
        "</svg>")
    },
    {
      // NOTE: checksum varies due to differing fp32 and imageStore()
      // implementations
      .name      = "rasters_prefix_fix", // bug:39620
      .surface   = { 1024, 300 },
      .checksums = {
        { 0xFD0B4012, {
            { param::INTEL,  {}                    },  // all intel
            { param::AMD,    { param::AMD_V1807B } } } // AMD/V1807B (Mesa)
        },
        { 0xFCF529FC, {
            { param::NVIDIA, {}                    } } // all nvidia
        },
      },
      .test = std::make_shared<test>(
        "<svg xmlns=\"http://www.w3.org/2000/svg\">\n"
        "  <g fill=\"black\"\n"
        "     transform=\"translate(-900,-950)\n"
        "                 scale(0.03125)\n"
        "                 matrix(-63986.14, -1331.7272, 1331.7272, -63986.14, 48960.0, 33920.0)\">\n"
        "    <polyline points =\n"
        "              \"-0.08,-0.02 0.28,-0.02 0.28,-0.02 0.28,0.02\n"
        "               0.28,0.02 -0.08,0.02 -0.08,0.02 -0.08,-0.02\"/>\n"
        "  </g>\n"
        "</svg>\n")
    },
    {
      .name      = "evenodd", // bug:42114
      .surface   = { 256, 256 },
      .checksums = {
        { 0x8FFF0070, {} }
      },
      .test = std::make_shared<test>(
        "<svg xmlns=\"http://www.w3.org/2000/svg\">\n"
        "  <path fill-rule=\"nonzero\" d=\"M24,8  h8 v8 h-8 z\n"
        "                                  M26,10 h4 v4 h-4 z\"/>\n"
        "  <path fill-rule=\"evenodd\" d=\"M8,8   h8 v8 h-8 z\n"
        "                                  M10,10 h4 v4 h-4 z\"/>\n"
        "</svg>\n")
    },
    {
      .name             = "composition_clip", // bug:25525
      .surface          = { 256, 256 },
      .clip.composition = { 0, 0, 128, 128 },
      .checksums        = {
        { 0xBFFF3840, {} }
      },
      .test = std::make_shared<test>(
        "<svg xmlns=\"http://www.w3.org/2000/svg\">\n"
        "  <path fill-rule=\"nonzero\" d=\"M8,8 h240 v240 h-240 z\"/>\n"
        "</svg>\n")
    },
    {
      .name        = "render_clip", // bug:25525
      .surface     = { 256, 256 },
      .clip.render = { 0, 0, 128, 128 },
      .checksums   = {
        { 0xBFFF3840, {} }
      },
      .test = std::make_shared<test>(
        "<svg xmlns=\"http://www.w3.org/2000/svg\">\n"
        "  <path fill-rule=\"nonzero\" d=\"M8,8 h240 v240 h-240 z\"/>\n"
        "</svg>\n")
    },
  };

//
//
//
INSTANTIATE_TEST_SUITE_P(spinel_vk_svg_tests,  //
                         spinel_vk_svg,        //
                         ::testing::ValuesIn(params),
                         spinel_vk_svg::param_name);

}  // namespace spinel::vk::test

//
//
//
