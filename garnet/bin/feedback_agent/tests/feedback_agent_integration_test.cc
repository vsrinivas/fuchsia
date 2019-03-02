// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ostream>

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/component/cpp/environment_services_helper.h>
#include <lib/escher/test/gtest_vulkan.h>
#include <zircon/errors.h>

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace fuchsia {
namespace feedback {
namespace {

// Smoke-tests the real environment service for the
// fuchsia.feedback.DataProvider FIDL interface, connecting through FIDL.
//
// We use VK_TEST instead of the regular TEST macro because Scenic needs Vulkan
// to operate properly and take a screenshot. Note that calls to Scenic hang
// indefinitely for headless devices so this test assumes the device has a
// display like the other Scenic tests, see SCN-1281.
VK_TEST(FeedbackAgentIntegrationTest, SmokeTest) {
  DataProviderSyncPtr feedback_data_provider;
  auto environment_services = component::GetEnvironmentServices();
  environment_services->ConnectToService(feedback_data_provider.NewRequest());

  std::unique_ptr<Screenshot> out_screenshot;
  ASSERT_EQ(feedback_data_provider->GetScreenshot(ImageEncoding::PNG,
                                                  &out_screenshot),
            ZX_OK);
  // We cannot expect a particular payload in the response because depending on
  // the device on which the test runs, Scenic might return a screenshot or not.
}

}  // namespace

}  // namespace feedback
}  // namespace fuchsia
