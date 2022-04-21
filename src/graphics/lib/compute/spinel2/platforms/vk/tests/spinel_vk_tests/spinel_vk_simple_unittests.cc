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
  std::function<void(spinel_path_builder_t)> paths_pfn;

  spinel_path_t   path;
  spinel_raster_t raster;

  test_spinel_vk_simple(std::function<void(spinel_path_builder_t)> paths_pfn) : paths_pfn(paths_pfn)
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
  paths_create(spinel_path_builder_t pb)
  {
    spinel(path_builder_begin(pb));

    paths_pfn(pb);

    spinel(path_builder_end(pb, &path));

    // this isn't necessary but can start work earlier
    spinel(path_builder_flush(pb));
  }

  void
  rasters_create(spinel_raster_builder_t rb, struct spinel_transform_stack * const ts)
  {
    spinel(raster_builder_begin(rb));

    struct spinel_clip const raster_clips[] = { { 0.0f, 0.0f, FLT_MAX, FLT_MAX } };

    spinel(raster_builder_add(rb,  //
                              &path,
                              spinel_transform_stack_top_weakref(ts),
                              (spinel_transform_t *)spinel_transform_stack_top_transform(ts),
                              NULL,
                              raster_clips,
                              1));

    spinel(raster_builder_end(rb, &raster));

    // this isn't necessary but can start work earlier
    spinel(raster_builder_flush(rb));
  }

  void
  layers_create(spinel_composition_t composition, spinel_styling_t styling, bool is_srgb)
  {
    //
    // define composition
    //
    spinel_layer_id layer_id = 0;

    spinel(composition_place(composition, &raster, &layer_id, NULL, 1));

    spinel_composition_seal(composition);

    //
    // define the styling
    //
    spinel_group_id group_id;

    spinel(styling_group_alloc(styling, &group_id));

    {
      spinel_styling_cmd_t * cmds_enter;

      spinel(styling_group_enter(styling, group_id, 1, &cmds_enter));

      cmds_enter[0] = SPN_STYLING_OPCODE_COLOR_ACC_ZERO;
    }

    {
      spinel_styling_cmd_t * cmds_leave;

      spinel(styling_group_leave(styling, group_id, 4, &cmds_leave));

      float const background[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

      // cmds[0-2]
      spinel_styling_background_over_encoder(cmds_leave, background);

      cmds_leave[3] = SPN_STYLING_OPCODE_COLOR_ACC_STORE_TO_SURFACE_RGBA8;
    }

    // this is the root group
    spinel(styling_group_parents(styling, group_id, 0, NULL));

    // the range of the root group is maximal [0,layer_count)
    spinel(styling_group_range_lo(styling, group_id, 0));
    spinel(styling_group_range_hi(styling, group_id, layer_count() - 1));

    // define layer styling
    {
      float rgba[4];

      color_rgb32_to_rgba_f32(rgba, 0x000000, 1.0f);  // Default to black

      if (is_srgb)
        {
          color_srgb_to_linear_rgb_f32(rgba);
        }

      color_premultiply_rgba_f32(rgba);

      spinel_styling_cmd_t * cmds;

      spinel(styling_group_layer(styling, group_id, layer_id, 5, &cmds));

      cmds[0] = SPN_STYLING_OPCODE_COVER_NONZERO;

      // encode solid fill and fp16v4 at cmds[1-3]
      spinel_styling_layer_fill_rgba_encoder(cmds + 1, rgba);

      cmds[4] = SPN_STYLING_OPCODE_BLEND_OVER;
    }

    // seal the styling
    spinel_styling_seal(styling);
  }

  void
  paths_dispose(spinel_context_t context)
  {
    spinel(path_release(context, &path, 1));
  }

  void
  rasters_dispose(spinel_context_t context)
  {
    spinel(raster_release(context, &raster, 1));
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
      [](spinel_path_builder_t pb) {
        spinel(path_builder_move_to(pb, 2.0f, 2.0f));
        spinel(path_builder_line_to(pb, 4.0f, 2.0f));
        spinel(path_builder_line_to(pb, 4.0f, 4.0f));
        spinel(path_builder_line_to(pb, 2.0f, 4.0f));
        spinel(path_builder_line_to(pb, 2.0f, 2.0f));
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
          // Spinel/Bifrost clips the surface to 2048x1024
          { param::ARM, { { param::ARM_MALI_G31, param::ARM_MALI_G31} } }
        }
      }
    },
    .test = std::make_shared<test>(
      [](spinel_path_builder_t pb) {
        //
        // This test can't be expressed with SVG because of its path
        // closing semantics so we put it here.
        //
        // This creates 48 staggered copies of a 2x1 pixel rectangle.
        //
        // The intent is to force multiple hash collisions in the
        // rasterizer.
        //
        for (float ii = 0.0f; ii < 48.0f; ii += 1.0f)
          {
            spinel(path_builder_move_to(pb, 15.0f + ii * 16.0f * 4.0f, 8.0f + ii));
            spinel(path_builder_line_to(pb, 15.0f + ii * 16.0f * 4.0f, 8.0f + ii + 1.0f));
            spinel(path_builder_line_to(pb, 17.0f + ii * 16.0f * 4.0f, 8.0f + ii + 1.0f));

            spinel(path_builder_move_to(pb, 17.0f + ii * 16.0f * 4.0f, 9.0f + ii));
            spinel(path_builder_line_to(pb, 17.0f + ii * 16.0f * 4.0f, 9.0f + ii - 1.0f));
            spinel(path_builder_line_to(pb, 15.0f + ii * 16.0f * 4.0f, 9.0f + ii - 1.0f));
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
