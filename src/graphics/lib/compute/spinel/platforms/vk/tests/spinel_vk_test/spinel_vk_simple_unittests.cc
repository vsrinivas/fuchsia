// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fxt_spinel_vk_render.h"
#include "spinel/ext/color/color.h"
#include "spinel/ext/transform_stack/transform_stack.h"

//
//
//

namespace spinel::vk::test {

//
// simple single path tests that can't be expressed with SVG
//
struct test_spinel_vk_simple : test_spinel_vk_render
{
  std::function<void(spn_path_builder_t)> paths_pfn;

  spn_path_t   path;
  spn_raster_t raster;

  test_spinel_vk_simple(std::function<void(spn_path_builder_t)> paths_pfn) : paths_pfn(paths_pfn)
  {
    ;
  }

  void
  create()
  {
    ;
  }

  void
  dispose()
  {
    ;
  }

  uint32_t
  layer_count()
  {
    return 1;
  }

  void
  paths_create(spn_path_builder_t pb)
  {
    spn(path_builder_begin(pb));

    paths_pfn(pb);

    spn(path_builder_end(pb, &path));

    // this isn't necessary but can start work earlier
    spn(path_builder_flush(pb));
  }

  void
  rasters_create(spn_raster_builder_t rb, struct transform_stack * const ts)
  {
    spn(raster_builder_begin(rb));

    struct spn_clip const raster_clips[] = { { 0.0f, 0.0f, FLT_MAX, FLT_MAX } };

    spn(raster_builder_add(rb,  //
                           &path,
                           transform_stack_top_weakref(ts),
                           (spn_transform_t *)transform_stack_top_transform(ts),
                           NULL,
                           raster_clips,
                           1));

    spn(raster_builder_end(rb, &raster));

    // this isn't necessary but can start work earlier
    spn(raster_builder_flush(rb));
  }

  void
  layers_create(spn_composition_t composition, spn_styling_t styling, bool is_srgb)
  {
    //
    // define composition
    //
    spn_layer_id layer_id = 0;

    spn(composition_place(composition, &raster, &layer_id, NULL, 1));

    spn_composition_seal(composition);

    //
    // define the styling
    //
    spn_group_id group_id;

    spn(styling_group_alloc(styling, &group_id));

    {
      spn_styling_cmd_t * cmds_enter;

      spn(styling_group_enter(styling, group_id, 1, &cmds_enter));

      cmds_enter[0] = SPN_STYLING_OPCODE_COLOR_ACC_ZERO;
    }

    {
      spn_styling_cmd_t * cmds_leave;

      spn(styling_group_leave(styling, group_id, 4, &cmds_leave));

      float const background[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

      // cmds[0-2]
      spn_styling_background_over_encoder(cmds_leave, background);

      cmds_leave[3] = SPN_STYLING_OPCODE_COLOR_ACC_STORE_TO_SURFACE;
    }

    // this is the root group
    spn(styling_group_parents(styling, group_id, 0, NULL));

    // the range of the root group is maximal [0,layer_count)
    spn(styling_group_range_lo(styling, group_id, 0));
    spn(styling_group_range_hi(styling, group_id, layer_count() - 1));

    // define layer styling
    {
      float rgba[4];

      color_rgb32_to_rgba_f32(rgba, 0x000000, 1.0f);

      if (is_srgb)
        {
          color_srgb_to_linear_rgb_f32(rgba);
        }

      color_premultiply_rgba_f32(rgba);

      spn_styling_cmd_t * cmds;

      spn(styling_group_layer(styling, group_id, layer_id, 5, &cmds));

      cmds[0] = SPN_STYLING_OPCODE_COVER_NONZERO;

      // encode solid fill and fp16v4 at cmds[1-3]
      spn_styling_layer_fill_rgba_encoder(cmds + 1, rgba);

      cmds[4] = SPN_STYLING_OPCODE_BLEND_OVER;
    }

    // seal the styling
    spn_styling_seal(styling);
  }

  void
  paths_dispose(spn_context_t context)
  {
    spn(path_release(context, &path, 1));
  }

  void
  rasters_dispose(spn_context_t context)
  {
    spn(raster_release(context, &raster, 1));
  }
};

//
// alias for test output aesthetics
//
using spinel_vk_simple = fxt_spinel_vk_render;
using param            = param_spinel_vk_render;
using test             = test_spinel_vk_simple;

//
// run parameterized tests
//
TEST_P(spinel_vk_simple, simple_tests)
{
  ;
}

//
// value parameterization is leveraged by the SVG fixture
//
param const params[] = {
  {
    .name      = "black_square_2x2",
    .surface   = { 1024, 1024 },  //
    .checksums = {                //
      { 0xFBF00004, {} }
    },
    .test = std::make_shared<test>(
      [](spn_path_builder_t pb) {
        spn(path_builder_move_to(pb, 2.0f, 2.0f));
        spn(path_builder_line_to(pb, 4.0f, 2.0f));
        spn(path_builder_line_to(pb, 4.0f, 4.0f));
        spn(path_builder_line_to(pb, 2.0f, 4.0f));
        spn(path_builder_line_to(pb, 2.0f, 2.0f));
      }),
  },
  {
    .name      = "tile_collision",  // fxb:43333
    .surface   = { 3096, 256 },     //
    .checksums = {                  //
      { 0x9FF3E860,
        {}
      },
      { 0xBFF3E840, {
          { param::ARM, { param::ARM_MALI_G31 } } // Spinel/Bifrost4 clips to a 2048x1024 surface
        }
      }
    },
    .test = std::make_shared<test>(
      [](spn_path_builder_t pb) {
        //
        // This test can't be expressed with SVG because of its path
        // closing semantics so we put it here.
        //
        // This creates 48 staggered copies of a 1x2 pixel rectangle.
        //
        // The intent is to force multiple hash collisions in the
        // rasterizer.
        //
        for (uint32_t ii = 0; ii < 48; ii++)
          {
            spn(path_builder_move_to(pb, 15.0f + ii * 16 * 4, 8.0f + ii));
            spn(path_builder_line_to(pb, 15.0f + ii * 16 * 4, 8.0f + ii + 1));

            spn(path_builder_move_to(pb, 17.0f + ii * 16 * 4, 9.0f + ii));
            spn(path_builder_line_to(pb, 17.0f + ii * 16 * 4, 9.0f + ii - 1));
          }
      }),
  },
};

//
//
//
INSTANTIATE_TEST_SUITE_P(spinel_vk_simple_tests,  //
                         spinel_vk_simple,        //
                         ::testing::ValuesIn(params),
                         spinel_vk_simple::param_name);

}  // namespace spinel::vk::test

//
//
//
