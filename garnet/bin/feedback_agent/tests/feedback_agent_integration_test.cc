// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ostream>

#include <fuchsia/feedback/cpp/fidl.h>
#include <garnet/public/lib/fostr/fidl/fuchsia/feedback/formatting.h>
#include <gtest/gtest.h>
#include <lib/component/cpp/environment_services_helper.h>
#include <lib/escher/test/gtest_vulkan.h>
#include <zircon/errors.h>

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

  Status out_status = Status::UNKNOWN;
  std::unique_ptr<PngImage> out_image;
  ASSERT_EQ(feedback_data_provider->GetPngScreenshot(&out_status, &out_image),
            ZX_OK);
  // We cannot expect a particular status and payload because depending on the
  // device on which the test runs, Scenic might return a screenshot or not.
  //
  // But the status should have been overwritten so we check it is no longer
  // UNKNOWN.
  EXPECT_NE(out_status, Status::UNKNOWN);
}

}  // namespace

// Pretty-prints status in gTest matchers instead of the default byte string in
// case of failed expectations.
void PrintTo(const Status status, std::ostream* os) { *os << status; }

}  // namespace feedback
}  // namespace fuchsia
