// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/integration_tests/mock_runner_registry.h"

namespace component {
namespace testing {

void MockRunnerRegistry::Register(
    ::fidl::InterfaceHandle<mockrunner::MockRunner> runner) {
  connect_count_++;
  auto runner_ptr = runner.Bind();
  runner_ptr.set_error_handler([this]() {
    dead_runner_count_++;
    runner_.reset();
  });
  runner_ = std::make_unique<MockRunnerWrapper>(std::move(runner_ptr));
}

}  // namespace testing
}  // namespace component
