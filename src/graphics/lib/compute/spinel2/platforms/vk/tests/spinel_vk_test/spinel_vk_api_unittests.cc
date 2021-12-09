// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fxt_spinel_vk.h"

//
//
//

namespace spinel::vk::test {

// alias for test output aesthetics
using spinel_vk_api = fxt_spinel_vk;

////////////////////////////////////////////////////////////////////
//
// CONTEXT
//

//
// the context is created and released by the fixture
//
TEST_F(spinel_vk_api, context)
{
  SUCCEED();
}

////////////////////////////////////////////////////////////////////
//
// PATH BUILDER
//

//
// create/release
//
TEST_F(spinel_vk_api, path_builder)
{
  spinel_path_builder_t pb;

  spinel(path_builder_create(context, &pb));

  spinel(path_builder_release(pb));
}

//
// define a tiny path
//
TEST_F(spinel_vk_api, path_builder_tiny)
{
  spinel_path_builder_t pb;

  spinel(path_builder_create(context, &pb));

  spinel(path_builder_begin(pb));

  // define a triangle
  spinel(path_builder_move_to(pb, 0.0f, 0.0f));
  spinel(path_builder_line_to(pb, 8.0f, 8.0f));
  spinel(path_builder_line_to(pb, 0.0f, 8.0f));
  spinel(path_builder_line_to(pb, 0.0f, 0.0f));

  spinel_path_t path;

  spinel(path_builder_end(pb, &path));

  //
  // release the path
  //
  spinel(path_release(context, &path, 1));

  //
  // release the path builder
  //
  spinel(path_builder_release(pb));
}

//
// expect errors if the path isn't begun
//
TEST_F(spinel_vk_api, path_builder_not_begun)
{
  spinel_path_builder_t pb;

  spinel(path_builder_create(context, &pb));

  // all should return errors
  EXPECT_EQ(spinel_path_builder_move_to(pb, 0.0f, 0.0f),  //
            SPN_ERROR_PATH_BUILDER_PATH_NOT_BEGUN);

  EXPECT_EQ(spinel_path_builder_line_to(pb, 0.0f, 0.0f),  //
            SPN_ERROR_PATH_BUILDER_PATH_NOT_BEGUN);

  EXPECT_EQ(spinel_path_builder_quad_to(pb, 0.0f, 0.0f, 0.0f, 0.0f),
            SPN_ERROR_PATH_BUILDER_PATH_NOT_BEGUN);

  EXPECT_EQ(spinel_path_builder_cubic_to(pb, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
            SPN_ERROR_PATH_BUILDER_PATH_NOT_BEGUN);

  EXPECT_EQ(spinel_path_builder_rat_quad_to(pb, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
            SPN_ERROR_PATH_BUILDER_PATH_NOT_BEGUN);

  EXPECT_EQ(spinel_path_builder_rat_cubic_to(pb, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
            SPN_ERROR_PATH_BUILDER_PATH_NOT_BEGUN);

  //
  // release the path builder
  //
  spinel(path_builder_release(pb));
}

//
// define one extremely long path to force an error and permanently
// "lose" the path builder
//
TEST_F(spinel_vk_api, path_builder_lost)
{
  spinel_path_builder_t pb;

  spinel(path_builder_create(context, &pb));

  //
  // generate one extremely long path to force an error and permanently
  // "lose" the path builder
  //
  spinel(path_builder_begin(pb));

  spinel(path_builder_move_to(pb, 0.0f, 0.0f));

  spinel_result_t result;

  do
    {
      result = spinel_path_builder_line_to(pb, 8.0f, 8.0f);
  } while (result == SPN_SUCCESS);

  //
  // the path builder has been lost
  //
  ASSERT_EQ(result, SPN_ERROR_PATH_BUILDER_LOST);

  //
  // attempt to further use the lost path builder
  //

  //
  // expect path builder lost
  //
  spinel_path_t path;

  ASSERT_EQ(spinel_path_builder_end(pb, &path), SPN_ERROR_PATH_BUILDER_LOST);

  //
  // expect the path handle to be marked invalid
  //
  // NOTE(allanmac): directly inspecting the .handle member is abnormal
  //
  ASSERT_EQ(path.handle, SPN_PATH_INVALID.handle);

  //
  // attempt to release the invalid handle
  //
  ASSERT_EQ(spinel_path_release(context, &path, 1), SPN_ERROR_HANDLE_INVALID);

  //
  // release the path builder
  //
  spinel(path_builder_release(pb));
}

//
// fxr:344936
//
TEST_F(spinel_vk_api, dispatch_implicit_rasters_flush)
{
  //
  // create the builders
  //
  spinel_path_builder_t pb;

  spinel(path_builder_create(context, &pb));

  spinel_raster_builder_t rb;

  spinel(raster_builder_create(context, &rb));

  //
  // how many to trip bug?
  //
  uint32_t const count = 255 * 2 + 1;  // +0 succeeds

  //
  // Create paths
  //
  spinel_path_t paths[count];

  for (uint32_t ii = 0; ii < count; ii++)
    {
      spinel(path_builder_begin(pb));

      float const ii_f32 = (float)ii;

      spinel(path_builder_move_to(pb, 0.0f + ii_f32, 0.0f));
      spinel(path_builder_line_to(pb, 8.0f + ii_f32, 8.0f));
      spinel(path_builder_line_to(pb, 0.0f + ii_f32, 8.0f));
      spinel(path_builder_line_to(pb, 0.0f + ii_f32, 0.0f));

      spinel(path_builder_end(pb, paths + ii));
    }

  //
  // Create rasters
  //
  // Note that a raster cohort is limited to 255 rasters.
  //
  // This results in the first 255 being flushed which, in turn, forces
  // the path builder to flush.
  //
  spinel_raster_t rasters[count];

  spinel_transform_weakref_t transform_weakref = SPN_TRANSFORM_WEAKREF_INVALID;
  spinel_transform_t const   transform         = {

    32.0f, 0.00f, 0.00f,  //
    0.00f, 32.0f, 0.00f,  //
    0.00f, 0.00f
  };

  spinel_clip_weakref_t clip_weakref = SPN_CLIP_WEAKREF_INVALID;
  spinel_clip_t const   clip         = { 0.0f, 0.0f, 0.0f, 0.0f };

  for (uint32_t ii = 0; ii < count; ii++)
    {
      spinel(raster_builder_begin(rb));

      spinel(raster_builder_add(rb,  //
                                paths + ii,
                                &transform_weakref,
                                &transform,
                                &clip_weakref,
                                &clip,
                                1));

      spinel(raster_builder_end(rb, rasters + ii));
    }

  //
  // force flush -- not normally done
  //
  spinel(raster_builder_flush(rb));

  //
  // release everything
  //
  spinel(raster_release(context, rasters, count));
  spinel(path_release(context, paths, count));

  //
  // release the builders
  //
  spinel(raster_builder_release(rb));
  spinel(path_builder_release(pb));
}

//
// Work-in-progress path is lost: fxb:46116
//

TEST_F(spinel_vk_api, wip_path_is_lost)
{
  spinel_path_builder_t pb;

  spinel(path_builder_create(context, &pb));

  //
  // generate 2 paths:
  //
  //   - path #1 is simple
  //   - path #2 is:
  //     - the path is started
  //     - the path builder is flushed
  //     - the path is continued
  //
  spinel_path_t paths[2];

  //
  // path #1: generate a simple path
  //
  // this will occupy 2 blocks
  //
  spinel(path_builder_begin(pb));

  spinel(path_builder_move_to(pb, 0.0f, 0.0f));
  spinel(path_builder_line_to(pb, 8.0f, 8.0f));
  spinel(path_builder_line_to(pb, 0.0f, 8.0f));
  spinel(path_builder_line_to(pb, 0.0f, 0.0f));

  spinel(path_builder_end(pb, paths + 0));

  //
  // path #2: start the path
  //
  spinel(path_builder_begin(pb));

  spinel(path_builder_move_to(pb, 0.0f, 0.0f));
  spinel(path_builder_line_to(pb, 8.0f, 8.0f));
  spinel(path_builder_line_to(pb, 0.0f, 8.0f));
  spinel(path_builder_line_to(pb, 0.0f, 0.0f));

  spinel(path_builder_flush(pb));

  spinel(path_builder_end(pb, paths + 1));

  //
  // release paths
  //
  spinel(path_release(context, paths, 2));

  //
  // release the builders
  //
  spinel(path_builder_release(pb));
}

//
//
//
}  // namespace spinel::vk::test

//
//
//
