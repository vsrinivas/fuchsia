// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/component/cpp/testing/test_with_environment.h"

#include "lib/component/cpp/environment_services_helper.h"
#include "lib/component/cpp/testing/test_util.h"
#include "termination_result.h"

namespace component {
namespace testing {

TestWithEnvironment::TestWithEnvironment()
    : real_services_(component::GetEnvironmentServices()) {
  real_services_->ConnectToService(real_env_.NewRequest());
  real_env_->GetLauncher(real_launcher_.NewRequest());
}

void TestWithEnvironment::CreateComponentInCurrentEnvironment(
    fuchsia::sys::LaunchInfo launch_info,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> request) {
  real_launcher_.CreateComponent(std::move(launch_info), std::move(request));
}

bool TestWithEnvironment::RunComponentUntilTerminatedOrTimeout(
    fuchsia::sys::ComponentControllerPtr component_controller,
    TerminationResult* termination_result, zx::duration timeout,
    zx::duration step) {
  bool is_terminated = false;
  component_controller.events().OnTerminated =
      [&](int64_t return_code, fuchsia::sys::TerminationReason reason) {
        is_terminated = true;
        if (termination_result != nullptr) {
          *termination_result = {
              .return_code = return_code,
              .reason = reason,
          };
        }
      };
  RunLoopWithTimeoutOrUntil([&]() { return is_terminated; }, timeout, step);
  return is_terminated;
}

}  // namespace testing
}  // namespace component
