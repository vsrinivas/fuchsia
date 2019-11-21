// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "env_spn_vk_target.h"

#include <stdlib.h>

#include "common/macros.h"
#include "common/vk/assert.h"
#include "common/vk/debug.h"
#include "spinel_vk_find_target.h"

//
//
//

using namespace spinel::vk::test;

//
//
//

env_spn_vk_target::env_spn_vk_target(env_vk_instance * const instance)  //
    : instance(instance)
{
  ;
}

//
//
//

void
env_spn_vk_target::SetUp()
{
  char error_buffer[64];

  bool const is_found = spn_vk_find_target(instance->vk.pdp.vendorID,
                                           instance->vk.pdp.deviceID,
                                           &spn,
                                           &hs,
                                           error_buffer,
                                           ARRAY_LENGTH_MACRO(error_buffer));
  ASSERT_TRUE(is_found);

#ifdef __Fuchsia__

#else

#endif
}

//
//
//

void
env_spn_vk_target::TearDown()
{
#ifdef __Fuchsia__

#else

#endif
}

//
//
//
