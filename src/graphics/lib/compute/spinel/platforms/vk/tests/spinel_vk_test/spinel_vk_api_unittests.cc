// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fxt_spinel_vk.h"

//
//
//

namespace spinel::vk::test {

// alias for test output aesthetics
using spinel_vk = fxt_spinel_vk;

////////////////////////////////////////////////////////////////////
//
// CONTEXT
//

//
// the context is created and released by the fixture
//
TEST_F(spinel_vk, context)
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
TEST_F(spinel_vk, path_builder)
{
  spn_path_builder_t pb;

  spn(path_builder_create(context, &pb));

  spn(path_builder_release(pb));
}

//
// define a tiny path
//
TEST_F(spinel_vk, path_builder_tiny)
{
  spn_path_builder_t pb;

  spn(path_builder_create(context, &pb));

  spn(path_builder_begin(pb));

  // define a triangle
  spn(path_builder_move_to(pb, 0.0f, 0.0f));
  spn(path_builder_line_to(pb, 8.0f, 8.0f));
  spn(path_builder_line_to(pb, 0.0f, 8.0f));
  spn(path_builder_line_to(pb, 0.0f, 0.0f));

  spn_path_t path;

  spn(path_builder_end(pb, &path));

  //
  // release the path
  //
  spn(path_release(context, &path, 1));

  //
  // release the path builder
  //
  spn(path_builder_release(pb));
}

//
// expect errors if the path isn't begun
//
TEST_F(spinel_vk, path_builder_not_begun)
{
  spn_path_builder_t pb;

  spn(path_builder_create(context, &pb));

  // all should return errors
  EXPECT_EQ(spn_path_builder_move_to(pb, 0.0f, 0.0f),  //
            SPN_ERROR_PATH_BUILDER_PATH_NOT_BEGUN);

  EXPECT_EQ(spn_path_builder_line_to(pb, 0.0f, 0.0f),  //
            SPN_ERROR_PATH_BUILDER_PATH_NOT_BEGUN);

  EXPECT_EQ(spn_path_builder_quad_to(pb, 0.0f, 0.0f, 0.0f, 0.0f),
            SPN_ERROR_PATH_BUILDER_PATH_NOT_BEGUN);

  EXPECT_EQ(spn_path_builder_cubic_to(pb, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
            SPN_ERROR_PATH_BUILDER_PATH_NOT_BEGUN);

  EXPECT_EQ(spn_path_builder_rat_quad_to(pb, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
            SPN_ERROR_PATH_BUILDER_PATH_NOT_BEGUN);

  EXPECT_EQ(spn_path_builder_rat_cubic_to(pb, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
            SPN_ERROR_PATH_BUILDER_PATH_NOT_BEGUN);

  //
  // release the path builder
  //
  spn(path_builder_release(pb));
}

//
// define one extremely long path to force an error and permanently
// "lose" the path builder
//
TEST_F(spinel_vk, path_builder_lost)
{
  spn_path_builder_t pb;

  spn(path_builder_create(context, &pb));

  //
  // generate one extremely long path to force an error and permanently
  // "lose" the path builder
  //
  spn(path_builder_begin(pb));

  spn(path_builder_move_to(pb, 0.0f, 0.0f));

  spn_result_t result;

  do
    {
      result = spn_path_builder_line_to(pb, 8.0f, 8.0f);
    }
  while (result == SPN_SUCCESS);

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
  spn_path_t path;

  ASSERT_EQ(spn_path_builder_end(pb, &path), SPN_ERROR_PATH_BUILDER_LOST);

  //
  // expect the path handle to be marked invalid
  //
  // NOTE(allanmac): directly inspecting the .handle member is abnormal
  //
  ASSERT_EQ(path.handle, SPN_PATH_INVALID.handle);

  //
  // attempt to release the invalid handle
  //
  ASSERT_EQ(spn_path_release(context, &path, 1), SPN_ERROR_HANDLE_INVALID);

  //
  // release the path builder
  //
  spn(path_builder_release(pb));
}

//
// go/fxr/344936
//
TEST_F(spinel_vk, dispatch_implicit_rasters_flush)
{
  //
  // create the builders
  //
  spn_path_builder_t pb;

  spn(path_builder_create(context, &pb));

  spn_raster_builder_t rb;

  spn(raster_builder_create(context, &rb));

  //
  // how many to trip bug?
  //
  uint32_t const count = 255 * 2 + 1; // +0 succeeds

  //
  // Create paths
  //
  spn_path_t paths[count];

  for (uint32_t ii = 0; ii < count; ii++)
    {
      spn(path_builder_begin(pb));

      spn(path_builder_move_to(pb, 0.0f + ii, 0.0f));
      spn(path_builder_line_to(pb, 8.0f + ii, 8.0f));
      spn(path_builder_line_to(pb, 0.0f + ii, 8.0f));
      spn(path_builder_line_to(pb, 0.0f + ii, 0.0f));

      spn(path_builder_end(pb, paths + ii));
    }

  //
  // Create rasters
  //
  // Note that a raster cohort is limited to 255 rasters.
  //
  // This results in the first 255 being flushed which, in turn, forces
  // the path builder to flush.
  //
  spn_raster_t rasters[count];

  spn_transform_weakref_t transform_weakref = SPN_TRANSFORM_WEAKREF_INVALID;
  spn_transform_t const   transform  //
    = { 32.0f, 0.00f, 0.00f,         //
        0.00f, 32.0f, 0.00f,         //
        0.00f, 0.00f };

  spn_clip_weakref_t clip_weakref = SPN_CLIP_WEAKREF_INVALID;
  spn_clip_t const   clip  //
    = { 0.0f, 0.0f, 0.0f, 0.0f };

  for (uint32_t ii = 0; ii < count; ii++)
    {
      spn(raster_builder_begin(rb));

      spn(raster_builder_add(rb,  //
                             paths + ii,
                             &transform_weakref,
                             &transform,
                             &clip_weakref,
                             &clip,
                             1));

      spn(raster_builder_end(rb, rasters + ii));
    }

  //
  // force flush -- not normally done
  //
  spn(raster_builder_flush(rb));

  //
  // drain everything
  //
  spn(vk_context_wait(context, 0, NULL, true, 10L * 1000L * 1000L * 1000L));
  spn(vk_context_wait(context, 0, NULL, true, 10L * 1000L * 1000L * 1000L));

  //
  // release everything
  //
  spn(raster_release(context, rasters, count));
  spn(path_release(context, paths, count));

  spn(vk_context_wait(context, 0, NULL, true, 10L * 1000L * 1000L * 1000L));

  //
  // release the builders
  //
  spn(raster_builder_release(rb));
  spn(path_builder_release(pb));
}

//
//
//

}  // namespace spinel::vk::test

//
//
//
