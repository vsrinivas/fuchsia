// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/cloud_provider_memory_diff/cpp/cloud_controller_factory.h"

#include <fuchsia/ledger/cloud/test/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-testing/test_loop.h>

#include "src/lib/callback/set_when_called.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace cloud_provider {
namespace {

using fuchsia::ledger::cloud::test::CloudControllerFactoryPtr;
using fuchsia::ledger::cloud::test::CloudControllerPtr;
using CloudControllerFactoryTest = gtest::TestLoopFixture;

TEST_F(CloudControllerFactoryTest, Launch) {
  CloudControllerFactoryPtr cloud_controller_factory;
  async_test_subloop_t* subloop =
      NewCloudControllerFactory(cloud_controller_factory.NewRequest(), 42);
  ASSERT_TRUE(subloop);
  auto token = test_loop().RegisterLoop(subloop);

  CloudControllerPtr cloud_controller;
  cloud_controller_factory->Build(cloud_controller.NewRequest());

  bool called;
  cloud_controller->SetNetworkState(NetworkState::DISCONNECTED, callback::SetWhenCalled(&called));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
}

}  // namespace
}  // namespace cloud_provider
