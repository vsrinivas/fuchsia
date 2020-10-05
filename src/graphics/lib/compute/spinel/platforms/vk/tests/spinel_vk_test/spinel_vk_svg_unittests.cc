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
          { param::ARM,    {}                    }, // all arm
          { param::INTEL,  {}                    }, // all intel
          { param::AMD,    { param::AMD_V1807B } }  // AMD/V1807B (Mesa)
        }
      },
      { 0xFCF529FC, {
          { param::NVIDIA, {}                    }  // all nvidia
        }
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
          { param::ARM,    {}                    }, // all arm
          { param::INTEL,  {}                    }  // all intel
        }
      },
      { 0xEE9805B8, {
          { param::NVIDIA, {}                    }  // all nvidia
        }
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
          { param::ARM,    {}                    }, // all arm
          { param::INTEL,  {}                    }  // all intel
        }
      },
      { 0xBED44623, {
          { param::NVIDIA, {}                    }  // all nvidia
        }
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
          { param::ARM,    {}                    }, // all arm
          { param::INTEL,  {}                    }  // all intel
        }
      },
      { 0xF8AFC987, {
          { param::NVIDIA, {}                    }  // all nvidia
        }
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
      { 0xB7841DF8, {
          { param::ARM,    {}                    }  // all arm
        }
      },
      { 0xB783FDD8, {
          { param::INTEL,  {}                    }  // all intel
        }
      },
      { 0xB69EC4A9, {
          { param::NVIDIA, {}                    }  // all nvidia
        }
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
  {
    .name        = "circles",
    .surface     = { 1024, 1024 },
    .checksums = {
      { 0xE86BA68F, {
          { param::ARM,    {}                    }, // all arm
          { param::INTEL,  {}                    }  // all intel
        }
      },
      { 0xE8458069, {
          { param::NVIDIA, {}                    }  // all nvidia
        }
      },
    },
    .test = std::make_shared<test>(
      "<svg version=\"1.1\" xmlns=\"http://www.w3.org/2000/svg\">\n"
      "  <circle cx=\"16\"  cy=\"512\" r=\"16\"/>\n"
      "  <circle cx=\"64\"  cy=\"512\" r=\"32\"/>\n"
      "  <circle cx=\"160\" cy=\"512\" r=\"64\"/>\n"
      "  <circle cx=\"352\" cy=\"512\" r=\"128\"/>\n"
      "  <circle cx=\"736\" cy=\"512\" r=\"256\"/>\n"
      "</svg>\n")
  },
  {
    .name        = "ellipses",
    .surface     = { 1024, 1024 },

    .checksums = {
      { 0xCB49AF86, {
          { param::ARM,    {}                    }, // all arm
          { param::INTEL,  {}                    }  // all intel
        }
      },
      { 0xCAFA6037, {
          { param::NVIDIA, {}                    }  // all nvidia
        }
      },
    },
    .test = std::make_shared<test>(
      "<svg version=\"1.1\" xmlns=\"http://www.w3.org/2000/svg\">\n"
      "  <ellipse cx=\"16\"  cy=\"512\" rx=\"16\"  ry=\"32\" />\n"
      "  <ellipse cx=\"64\"  cy=\"512\" rx=\"32\"  ry=\"64\" />\n"
      "  <ellipse cx=\"160\" cy=\"512\" rx=\"64\"  ry=\"128\"/>\n"
      "  <ellipse cx=\"352\" cy=\"512\" rx=\"128\" ry=\"256\"/>\n"
      "  <ellipse cx=\"736\" cy=\"512\" rx=\"256\" ry=\"512\"/>\n"
      "</svg>\n")
  },
  {
    .name        = "arcs",
    .surface     = { 1024, 512 },
    .checksums = {
      { 0xC2E4C4A9, {
          { param::ARM,    {}                    }  // all arm
        }
      },
      { 0xC2E4C3A9, {
          { param::INTEL,  {}                    }  // all intel
        }
      },
      { 0xC26C3E22, {
          { param::NVIDIA, {}                    }  // all nvidia
        }
      },
    },
    .test = std::make_shared<test>(
      "<svg version=\"1.1\" xmlns=\"http://www.w3.org/2000/svg\">\n"
      "  <!-- four cases -->\n"
      "  <g transform=\"translate(0,0)\">\n"
      "    <ellipse cx=\"125\" cy=\"125\" rx=\"100\" ry=\"50\" fill=\"green\"/>\n"
      "    <ellipse cx=\"225\" cy=\"75\" rx=\"100\" ry=\"50\" fill=\"blue\"/>\n"
      "    <path d=\"M 125,75 a100,50 0 0,0 100,50\" fill=\"red\"/>\n"
      "  </g>\n"
      "  <g transform=\"translate(300,0)\">\n"
      "    <ellipse cx=\"225\" cy=\"75\" rx=\"100\" ry=\"50\" fill=\"blue\"/>\n"
      "    <ellipse cx=\"125\" cy=\"125\" rx=\"100\" ry=\"50\" fill=\"green\"/>\n"
      "    <path d=\"M 125,75 a100,50 0 0,1 100,50\" fill=\"red\"/>\n"
      "  </g>\n"
      "  <g transform=\"translate(0,250)\">\n"
      "    <ellipse cx=\"225\" cy=\"75\" rx=\"100\" ry=\"50\" fill=\"blue\"/>\n"
      "    <ellipse cx=\"125\" cy=\"125\" rx=\"100\" ry=\"50\" fill=\"green\"/>\n"
      "    <path d=\"M 125,75 a100,50 0 1,0 100,50\" fill=\"red\"/>\n"
      "  </g>\n"
      "  <g transform=\"translate(300,250)\">\n"
      "    <ellipse cx=\"125\" cy=\"125\" rx=\"100\" ry=\"50\" fill=\"green\"/>\n"
      "    <ellipse cx=\"225\" cy=\"75\" rx=\"100\" ry=\"50\" fill=\"blue\"/>\n"
      "    <path d=\"M 125,75 a100,50 0 1,1 100,50\" fill=\"red\"/>\n"
      "  </g>\n"
      "  <!-- simple -->\n"
      "  <g transform=\"translate(640,0)\">\n"
      "    <path d=\"M80 80\n"
      "             A 45 45, 0, 0, 0, 125 125\n"
      "             L 125 80 Z\" fill=\"green\"/>\n"
      "    <path d=\"M230 80\n"
      "             A 45 45, 0, 1, 0, 275 125\n"
      "             L 275 80 Z\" fill=\"red\"/>\n"
      "    <path d=\"M80 230\n"
      "             A 45 45, 0, 0, 1, 125 275\n"
      "             L 125 230 Z\" fill=\"purple\"/>\n"
      "    <path d=\"M230 230\n"
      "             A 45 45, 0, 1, 1, 275 275\n"
      "             L 275 230 Z\" fill=\"blue\"/>\n"
      "  </g>\n"
      "  <!-- angled -->\n"
      "  <g transform=\"translate(675,225)\">\n"
      "    <path d=\"M 110 215\n"
      "             A 30 50 0 0 1 162.55 162.45 z\n"
      "             M 172.55 152.45\n"
      "             A 30 50 -45 0 1 215.1 109.9 z\"/>\n"
      "  </g>\n"
      "</svg>")
  },
  {
    .name        = "bifrost4",
    .surface     = { 600, 1024 },
    .checksums = {
      { 0xD526D15B, {
          { param::ARM,    {}                    }, // all arm
          { param::INTEL,  {}                    }, // all intel
          { param::NVIDIA, {}                    }  // all nvidia
        }
      },
    },
    .test = std::make_shared<test>(
        "<svg version=\"1.1\" xmlns=\"http://www.w3.org/2000/svg\">\n"
        "  <g transform=\"rotate(11,308,284) scale(1.0) translate(200,200)\">\n"
        "    <path d=\"M -16.81300000000002, 342.93499999999995\n"
        "             C -16.17700000000002, 346.405,             -14.10100000000002,   353.47799999999995, -7.31000000000002, 358.7919999999999\n"
        "             L  -6.47700000000002, 359.4439999999999\n"
        "             L  -6.5,              358.39\n"
        "             C  -6.741,            348.18,              -5.998,               331.775,            -2.976,            331.217\n"
        "             C  -2.231,            331.079,             -0.04599999999999982, 332.027,             4.128,            343.769\n"
        "             L   8.546,            361.894\n"
        "             Z\"\n"
        "          />\n"
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
