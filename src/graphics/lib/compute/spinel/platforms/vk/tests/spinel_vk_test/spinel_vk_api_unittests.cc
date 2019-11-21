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

//
// CONTEXT
//

TEST_F(spinel_vk, context)
{
  SUCCEED();
}

//
// PATH BUILDER
//

TEST_F(spinel_vk, path_builder)
{
  spn_path_builder_t pb;

  ASSERT_EQ(spn_path_builder_create(context, &pb), SPN_SUCCESS);

  ASSERT_EQ(spn_path_builder_release(pb), SPN_SUCCESS);
}

//
//
//

}  // namespace spinel::vk::test

//
//
//
