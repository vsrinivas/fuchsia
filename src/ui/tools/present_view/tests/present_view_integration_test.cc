// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>

#include <optional>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "lib/fidl/cpp/binding_set.h"
#include "lib/gtest/test_loop_fixture.h"
#include "lib/sys/cpp/testing/test_with_environment.h"

namespace present_view {

constexpr static char kManifestURI[] =
    "fuchsia-pkg://fuchsia.com/present_view#meta/present_view.cmx";

class PresentViewTest : public sys::testing::TestWithEnvironment {
 public:
  ~PresentViewTest() override = default;

 protected:
  fuchsia::sys::ComponentControllerPtr controller_;
  sys::testing::TerminationResult termination_reason_;
};

TEST_F(PresentViewTest, Startup) { EXPECT_TRUE(true); }

TEST_F(PresentViewTest, StartPresentViewWithoutLocale) {
  fuchsia::sys::LaunchInfo launch_info{
      .url = kManifestURI,
      .arguments =
          std::vector<std::string>{
              kManifestURI,  // We must go deeper!
          },
  };
  CreateComponentInCurrentEnvironment(std::move(launch_info), controller_.NewRequest());
  controller_.events().OnTerminated = [&](int64_t return_code,
                                          fuchsia::sys::TerminationReason reason) {
    termination_reason_ = {
        .return_code = return_code,
        .reason = reason,
    };
  };
  RunLoopWithTimeout();
  EXPECT_EQ(fuchsia::sys::TerminationReason::EXITED, termination_reason_.reason)
      << "return code: " << termination_reason_.return_code;
}

TEST_F(PresentViewTest, StartPresentViewWithLocale) {
  fuchsia::sys::LaunchInfo launch_info{
      .url = kManifestURI,
      .arguments =
          std::vector<std::string>{
              "--locale=en-US",
              kManifestURI,
          },
  };
  CreateComponentInCurrentEnvironment(std::move(launch_info), controller_.NewRequest());
  controller_.events().OnTerminated = [&](int64_t return_code,
                                          fuchsia::sys::TerminationReason reason) {
    termination_reason_ = {
        .return_code = return_code,
        .reason = reason,
    };
  };
  RunLoopWithTimeout();
  EXPECT_EQ(fuchsia::sys::TerminationReason::EXITED, termination_reason_.reason)
      << "return code: " << termination_reason_.return_code;
}

}  // namespace present_view
