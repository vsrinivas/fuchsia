// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "env_spinel_vk_target.h"

#include <stdlib.h>

#include "common/macros.h"
#include "common/vk/debug.h"
#include "spinel/platforms/vk/ext/find_target/find_target.h"

//
//
//

using namespace spinel::vk::test;

//
//
//

env_spinel_vk_target::env_spinel_vk_target(env_vk_instance * const instance)  //
    : instance(instance)
{
  ;
}

//
//
//

void
env_spinel_vk_target::SetUp()
{
  spinel = spinel_vk_find_target(instance->vk.pdp.vendorID, instance->vk.pdp.deviceID);

  ASSERT_TRUE(spinel != NULL);
}

//
//
//

void
env_spinel_vk_target::TearDown()
{
  spinel_vk_target_dispose(spinel);
}

//
//
//
