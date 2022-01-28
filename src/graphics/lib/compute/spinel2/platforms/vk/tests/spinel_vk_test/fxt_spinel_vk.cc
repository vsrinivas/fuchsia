// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fxt_spinel_vk.h"

#include "env.h"
#include "spinel/platforms/vk/spinel_vk.h"

//
//
//

using namespace spinel::vk::test;

//
//
//

env * fxt_spinel_vk::shared_env = nullptr;

void
fxt_spinel_vk::SetUpTestSuite()
{
  ASSERT_EQ(shared_env, nullptr);

  shared_env = new env();

  shared_env->GlobalSetUp();
}

void
fxt_spinel_vk::TearDownTestSuite()
{
  shared_env->GlobalTearDown();

  delete shared_env;

  shared_env = nullptr;
}

//
//
//

void
fxt_spinel_vk::SetUp()
{
  //
  // create Spinel context
  //
  struct spinel_vk_context_create_info const cci = {

    .vk = { .pd = shared_env->instance->vk.pd,
            .d  = shared_env->device->vk.d,
            .pc = shared_env->device->vk.pc,
            .ac = nullptr,
            .q  = { .compute = { .flags        = 0,  // One compute queue / nothing shared
                                 .family_index = 0,
                                 .count        = 1 } } },

    .target          = shared_env->target->spinel,
    .block_pool_size = 1 << 25,  // 32 MB
    .handle_count    = 1 << 15,  // 32K handles
  };

  context = spinel_vk_context_create(&cci);

  EXPECT_NE(context, nullptr);
}

//
//
//

void
fxt_spinel_vk::TearDown()
{
  spinel(context_release(context));
}

//
//
//
