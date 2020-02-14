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
  {
    .name        = "bezier_quads",
    .surface     = { 1024, 1024 },
    .checksums = {
      { 0xEE9E0BBE, {
          { param::INTEL,  {}                    } } // all intel
      },
      { 0xEE9805B8, {
          { param::NVIDIA, {}                    } } // all nvidia
      },
    },
    .test = std::make_shared<test>(
      "<svg xmlns=\"http://www.w3.org/2000/svg\">\n"
      "  <!-- collinear quads -->\n"
      "  <path d= \"M450,200\n"
      "            Q500,200 550,200\n"
      "            Q550,500 550,800\n"
      "            Q500,800 450,800\n"
      "            Q450,500 450,200\"/>\n"
      "  <!-- W3C SVG Paths: Quads -->\n"
      "  <path d=\"M100,200 Q250,100 400,200\"/>\n"
      "  <path d=\"M600,200 Q825,100 900,200\"/>\n"
      "  <path d=\"M600,800 Q675,700 750,800 T900,800\"/>\n"
      "</svg>\n")
  },
  {
    .name        = "bezier_cubics",
    .surface     = { 1024, 1024 },
    .checksums = {
      { 0xBEFA6C49, {
          { param::INTEL,  {}                    } } // all intel
      },
      { 0xBED44623, {
          { param::NVIDIA, {}                    } } // all nvidia
      },
    },
    .test = std::make_shared<test>(
      "<svg xmlns=\"http://www.w3.org/2000/svg\">\n"
      "  <!-- collinear cubics -->\n"
      "  <path d= \"M450,200\n"
      "            C500,200 500,200 550,200\n"
      "            C550,500 550,500 550,800\n"
      "            C500,800 500,800 450,800\n"
      "            C450,500 450,500 450,200\"/>\n"
      "  <!-- W3C SVG Paths: Cubics -->\n"
      "  <path d=\"M100,200 C100,100 400,100 400,200\"/>\n"
      "  <path d=\"M100,500 C 25,400 475,400 400,500\"/>\n"
      "  <path d=\"M100,800 C175,700 325,700 400,800\"/>\n"
      "  <path d=\"M600,200 C675,100 975,100 900,200\"/>\n"
      "  <path d=\"M600,500 C600,350 900,650 900,500\"/>\n"
      "  <path d=\"M600,800 C625,700 725,700 750,800 S875,900 900,800\"/>\n"
      "</svg>\n")
  },
  {
    .name        = "rational_quads",
    .surface     = { 1024, 1024 },
    .checksums = {
      { 0xF994CF80, {
          { param::INTEL,  {}                    } } // all intel
      },
      { 0xF8AFC987, {
          { param::NVIDIA, {}                    } } // all nvidia
      },
    },
    .test = std::make_shared<test>(
      "<svg xmlns=\"http://www.w3.org/2000/svg\">\n"
      "  <g transform=\"translate(16,16)\">\n"
      "    <g>\n"
      "      <rect width=\"592\" height=\"100\"/>\n"
      "      <g fill=\"green\" transform=\"translate(0,8)\">\n"
      "        <path d= \"v16 h128 v-16 z\" transform=\"translate( 16)\"/>\n"
      "        <path d= \"v16 h128 v-16 z\" transform=\"translate(160)\"/>\n"
      "        <path d= \"v16 h128 v-16 z\" transform=\"translate(304)\"/>\n"
      "        <path d= \"v16 h128 v-16 z\" transform=\"translate(448)\"/>\n"
      "      </g>\n"
      "      <g fill=\"blue\" transform=\"translate(0,24)\">\n"
      "        <path d= \"r64,64 128,0 +3.0 z\" transform=\"translate( 16)\"/>\n"
      "        <path d= \"r64,64 128,0 +1.0 z\" transform=\"translate(160)\"/>\n"
      "        <path d= \"r64,64 128,0 +0.3 z\" transform=\"translate(304)\"/>\n"
      "        <path d= \"r64,64 128,0 +0.0 z\" transform=\"translate(448)\"/>\n"
      "      </g>\n"
      "    </g>\n"
      "    <g transform=\"project(0.506757,-0.281532,0,1,0,200,-0.00112613,0)\">\n"
      "      <rect width=\"592\" height=\"100\"/>\n"
      "      <g fill=\"green\" transform=\"translate(0,8)\">\n"
      "        <path d= \"v16 h128 v-16 z\" transform=\"translate( 16)\"/>\n"
      "        <path d= \"v16 h128 v-16 z\" transform=\"translate(160)\"/>\n"
      "        <path d= \"v16 h128 v-16 z\" transform=\"translate(304)\"/>\n"
      "        <path d= \"v16 h128 v-16 z\" transform=\"translate(448)\"/>\n"
      "      </g>\n"
      "      <g fill=\"red\" transform=\"translate(0,24)\">\n"
      "        <path d= \"r64,64 128,0 +3.0 z\" transform=\"translate( 16)\"/>\n"
      "        <path d= \"r64,64 128,0 +1.0 z\" transform=\"translate(160)\"/>\n"
      "        <path d= \"r64,64 128,0 +0.3 z\" transform=\"translate(304)\"/>\n"
      "        <path d= \"r64,64 128,0 +0.0 z\" transform=\"translate(448)\"/>\n"
      "      </g>\n"
      "    </g>\n"
      "    <g transform=\"project(0.675676,0,-3.12,0,312,400,0,-0.006)\">\n"
      "      <rect width=\"592\" height=\"100\"/>\n"
      "      <g fill=\"green\" transform=\"translate(0,8)\">\n"
      "        <path d= \"v16 h128 v-16 z\" transform=\"translate( 16)\"/>\n"
      "        <path d= \"v16 h128 v-16 z\" transform=\"translate(160)\"/>\n"
      "        <path d= \"v16 h128 v-16 z\" transform=\"translate(304)\"/>\n"
      "        <path d= \"v16 h128 v-16 z\" transform=\"translate(448)\"/>\n"
      "      </g>\n"
      "      <g fill=\"yellow\" transform=\"translate(0,24)\">\n"
      "        <path d= \"r64,64 128,0 +3.0 z\" transform=\"translate( 16)\"/>\n"
      "        <path d= \"r64,64 128,0 +1.0 z\" transform=\"translate(160)\"/>\n"
      "        <path d= \"r64,64 128,0 +0.3 z\" transform=\"translate(304)\"/>\n"
      "        <path d= \"r64,64 128,0 +0.0 z\" transform=\"translate(448)\"/>\n"
      "      </g>\n"
      "    </g>\n"
      "  </g>\n"
      "</svg>\n")
  },
  {
    .name        = "rational_cubics",
    .surface     = { 1024, 1024 },
    .checksums = {
      { 0xB783FDD8, {
          { param::INTEL,  {}                    } } // all intel
      },
      { 0xB69EC4A9, {
          { param::NVIDIA, {}                    } } // all nvidia
      },
    },
    .test = std::make_shared<test>(
      "<svg xmlns=\"http://www.w3.org/2000/svg\">\n"
      "  <g transform=\"translate(16,16)\">\n"
      "    <g>\n"
      "      <rect width=\"592\" height=\"100\"/>\n"
      "      <g fill=\"green\" transform=\"translate(0,8)\">\n"
      "        <path d= \"v16 h128 v-16 z\" transform=\"translate( 16)\"/>\n"
      "        <path d= \"v16 h128 v-16 z\" transform=\"translate(160)\"/>\n"
      "        <path d= \"v16 h128 v-16 z\" transform=\"translate(304)\"/>\n"
      "        <path d= \"v16 h128 v-16 z\" transform=\"translate(448)\"/>\n"
      "      </g>\n"
      "      <g fill=\"blue\" transform=\"translate(0,24)\">\n"
      "        <path d= \"d32,68 96,68 128,0 +2.0 +2.0 z\" transform=\"translate( 16)\"/>\n"
      "        <path d= \"d32,68 96,68 128,0 +1.0 +1.0 z\" transform=\"translate(160)\"/>\n"
      "        <path d= \"d32,68 96,68 128,0 +0.5 +0.5 z\" transform=\"translate(304)\"/>\n"
      "        <path d= \"d32,68 96,68 128,0 +0.0 +0.0 z\" transform=\"translate(448)\"/>\n"
      "      </g>\n"
      "    </g>\n"
      "    <g transform=\"project(0.506757,-0.281532,0,1,0,200,-0.00112613,0)\">\n"
      "      <rect width=\"592\" height=\"100\"/>\n"
      "      <g fill=\"green\" transform=\"translate(0,8)\">\n"
      "        <path d= \"v16 h128 v-16 z\" transform=\"translate( 16)\"/>\n"
      "        <path d= \"v16 h128 v-16 z\" transform=\"translate(160)\"/>\n"
      "        <path d= \"v16 h128 v-16 z\" transform=\"translate(304)\"/>\n"
      "        <path d= \"v16 h128 v-16 z\" transform=\"translate(448)\"/>\n"
      "      </g>\n"
      "      <g fill=\"red\" transform=\"translate(0,24)\">\n"
      "        <path d= \"d32,68 96,68 128,0 +2.0 +2.0 z\" transform=\"translate( 16)\"/>\n"
      "        <path d= \"d32,68 96,68 128,0 +1.0 +1.0 z\" transform=\"translate(160)\"/>\n"
      "        <path d= \"d32,68 96,68 128,0 +0.5 +0.5 z\" transform=\"translate(304)\"/>\n"
      "        <path d= \"d32,68 96,68 128,0 +0.0 +0.0 z\" transform=\"translate(448)\"/>\n"
      "      </g>\n"
      "    </g>\n"
      "    <g transform=\"project(0.675676,0,-3.12,0,312,400,0,-0.006)\">\n"
      "      <rect width=\"592\" height=\"100\"/>\n"
      "      <g fill=\"green\" transform=\"translate(0,8)\">\n"
      "        <path d= \"v16 h128 v-16 z\" transform=\"translate( 16)\"/>\n"
      "        <path d= \"v16 h128 v-16 z\" transform=\"translate(160)\"/>\n"
      "        <path d= \"v16 h128 v-16 z\" transform=\"translate(304)\"/>\n"
      "        <path d= \"v16 h128 v-16 z\" transform=\"translate(448)\"/>\n"
      "      </g>\n"
      "      <g fill=\"yellow\" transform=\"translate(0,24)\">\n"
      "        <path d= \"d32,68 96,68 128,0 +2.0 +2.0 z\" transform=\"translate( 16)\"/>\n"
      "        <path d= \"d32,68 96,68 128,0 +1.0 +1.0 z\" transform=\"translate(160)\"/>\n"
      "        <path d= \"d32,68 96,68 128,0 +0.5 +0.5 z\" transform=\"translate(304)\"/>\n"
      "        <path d= \"d32,68 96,68 128,0 +0.0 +0.0 z\" transform=\"translate(448)\"/>\n"
      "      </g>\n"
      "    </g>\n"
      "  </g>\n"
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
