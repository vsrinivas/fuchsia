// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene_manager/tests/escher_test_environment.h"

#include "lib/escher/examples/common/demo_harness.h"

static constexpr uint32_t kScreenWidth = 2160;
static constexpr uint32_t kScreenHeight = 1440;

namespace scene_manager {
namespace test {

void EscherTestEnvironment::SetUp(std::string tests_name) {
  escher_demo_harness_ =
      DemoHarness::New(DemoHarness::WindowParams{tests_name, kScreenWidth,
                                                 kScreenHeight, 2, false},
                       DemoHarness::InstanceParams());
  escher_ =
      std::make_unique<escher::Escher>(escher_demo_harness_->device_queues());
}

void EscherTestEnvironment::TearDown() {
  escher_.reset();
  escher_demo_harness_->Shutdown();
}

}  // namespace test
}  // namespace scene_manager
