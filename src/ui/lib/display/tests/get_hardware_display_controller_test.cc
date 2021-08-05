// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/display/get_hardware_display_controller.h"

#include <lib/fpromise/promise.h>
#include <lib/fpromise/single_threaded_executor.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/test_with_environment_fixture.h>

#include <gtest/gtest.h>

#include "src/ui/lib/display/hardware_display_controller_provider_impl.h"

namespace ui_display {
namespace test {

struct fake_context : fpromise::context {
  fpromise::executor* executor() const override { return nullptr; }
  fpromise::suspended_task suspend_task() override { return fpromise::suspended_task(); }
};

class GetHardwareDisplayControllerTest : public gtest::TestWithEnvironmentFixture {};

TEST_F(GetHardwareDisplayControllerTest, ErrorCase) {
  auto promise = GetHardwareDisplayController();
  fake_context context;
  EXPECT_TRUE(promise(context).is_error());
}

TEST_F(GetHardwareDisplayControllerTest, WithHardwareDisplayControllerProviderImpl) {
  std::unique_ptr<sys::ComponentContext> app_context = sys::ComponentContext::Create();
  ui_display::HardwareDisplayControllerProviderImpl hdcp_service_impl(app_context.get());
  auto promise = GetHardwareDisplayController(&hdcp_service_impl);
  fake_context context;
  EXPECT_FALSE(promise(context).is_error());
}

}  // namespace test
}  // namespace ui_display
