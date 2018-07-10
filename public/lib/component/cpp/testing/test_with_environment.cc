// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/component/cpp/testing/test_with_environment.h"

#include "lib/component/cpp/testing/test_util.h"

namespace component {
namespace testing {

TestWithEnvironment::TestWithEnvironment() {
  ConnectToEnvironmentService(real_env_.NewRequest());
  real_env_->GetLauncher(real_launcher_.NewRequest());
}

void TestWithEnvironment::CreateComponentInCurrentEnvironment(
    fuchsia::sys::LaunchInfo launch_info,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> request) {
  real_launcher_.CreateComponent(std::move(launch_info), std::move(request));
}

}  // namespace testing
}  // namespace component
