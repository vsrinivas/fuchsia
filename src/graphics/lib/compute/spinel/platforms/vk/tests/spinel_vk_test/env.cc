// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "env.h"

//
//
//

using namespace spinel::vk::test;

//
// This is a mild attempt at mimicking how global test environments are
// set up and torn down.
//

env::env()
{
  instance = std::make_unique<env_vk_instance>(0, 0);
  target   = std::make_unique<env_spn_vk_target>(instance.get());
  device   = std::make_unique<env_vk_device>(instance.get(), target.get());
}

//
//
//

void
env::GlobalSetUp()
{
  // set up in order
  instance->SetUp();
  target->SetUp();
  device->SetUp();
}

//
//
//

void
env::GlobalTearDown()
{
  // tear down in reverse order
  device->TearDown();
  target->TearDown();
  instance->TearDown();
}

//
//
//
