// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "env.h"
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

  ASSERT_EQ(spn_path_builder_create(context, &pb), SPN_SUCCESS);

  ASSERT_EQ(spn_path_builder_release(pb), SPN_SUCCESS);
}

//
// define a tiny path
//
TEST_F(spinel_vk, path_builder_tiny)
{
  spn_path_builder_t pb;

  ASSERT_EQ(spn_path_builder_create(context, &pb), SPN_SUCCESS);

  ASSERT_EQ(spn_path_builder_begin(pb), SPN_SUCCESS);

  // define a triangle
  ASSERT_EQ(spn_path_builder_move_to(pb, 0.0f, 0.0f), SPN_SUCCESS);
  ASSERT_EQ(spn_path_builder_line_to(pb, 8.0f, 8.0f), SPN_SUCCESS);
  ASSERT_EQ(spn_path_builder_line_to(pb, 0.0f, 8.0f), SPN_SUCCESS);
  ASSERT_EQ(spn_path_builder_line_to(pb, 0.0f, 0.0f), SPN_SUCCESS);

  spn_path_t path;

  ASSERT_EQ(spn_path_builder_end(pb, &path), SPN_SUCCESS);

  //
  // release the path
  //
  ASSERT_EQ(spn_path_release(context, &path, 1), SPN_SUCCESS);

  //
  // release the path builder
  //
  ASSERT_EQ(spn_path_builder_release(pb), SPN_SUCCESS);
}

//
// expect errors if the path isn't begun
//
TEST_F(spinel_vk, path_builder_not_begun)
{
  spn_path_builder_t pb;

  ASSERT_EQ(spn_path_builder_create(context, &pb), SPN_SUCCESS);

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
  ASSERT_EQ(spn_path_builder_release(pb), SPN_SUCCESS);
}

//
// define one extremely long path to force an error and permanently
// "lose" the path builder
//
TEST_F(spinel_vk, path_builder_lost)
{
  spn_path_builder_t pb;

  ASSERT_EQ(spn_path_builder_create(context, &pb), SPN_SUCCESS);

  //
  // generate one extremely long path to force an error and permanently
  // "lose" the path builder
  //
  ASSERT_EQ(spn_path_builder_begin(pb), SPN_SUCCESS);

  ASSERT_EQ(spn_path_builder_move_to(pb, 0.0f, 0.0f), SPN_SUCCESS);

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
  ASSERT_EQ(spn_path_builder_release(pb), SPN_SUCCESS);
}

//
//
//

}  // namespace spinel::vk::test

//
//
//
