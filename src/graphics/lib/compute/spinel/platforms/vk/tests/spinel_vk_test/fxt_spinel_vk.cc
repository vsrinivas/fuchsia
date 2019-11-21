// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fxt_spinel_vk.h"

#include "env.h"
#include "spinel/spinel_assert.h"
#include "spinel/spinel_vk.h"

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
  ASSERT_EQ(shared_env,nullptr);

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
  struct spn_vk_environment spn_env = {

    .d    = shared_env->device->vk.d,
    .ac   = nullptr,
    .pc   = shared_env->device->vk.pc,
    .pd   = shared_env->instance->vk.pd,
    .pdmp = shared_env->instance->vk.pdmp,
    .qfi  = 0
  };

  struct spn_vk_context_create_info const spn_cci = {
    .spinel          = shared_env->target->spn,
    .hotsort         = shared_env->target->hs,
    .block_pool_size = 1 << 25,  // 32 MB (128K x 128-dword blocks)
    .handle_count    = 1 << 15,  // 32K handles
  };

  spn(vk_context_create(&spn_env, &spn_cci, &context));
}

//
//
//

void
fxt_spinel_vk::TearDown()
{
  spn(context_release(context));
}

//
//
//
