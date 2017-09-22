// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/device_runner/user_controller_impl.h"

#include "lib/app/fidl/application_launcher.fidl.h"
#include "peridot/lib/testing/test_with_message_loop.h"
#include "gtest/gtest.h"

namespace modular {
namespace {

class TestApplicationLauncher : app::ApplicationLauncher {
 public:
  explicit TestApplicationLauncher(
      fidl::InterfaceRequest<app::ApplicationLauncher> request)
      : binding_(this, std::move(request)) {}

  app::ApplicationLaunchInfoPtr last_launch_info;

 private:
  void CreateApplication(
      app::ApplicationLaunchInfoPtr launch_info,
      fidl::InterfaceRequest<app::ApplicationController> /*request*/) override {
    last_launch_info = std::move(launch_info);
  }

  fidl::Binding<app::ApplicationLauncher> binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApplicationLauncher);
};

class UserControllerImplTest : public testing::TestWithMessageLoop {};

TEST_F(UserControllerImplTest, StartUserRunner) {
  app::ApplicationLauncherPtr application_launcher_ptr;
  TestApplicationLauncher test_application_launcher(
      application_launcher_ptr.NewRequest());
  ASSERT_TRUE(test_application_launcher.last_launch_info.is_null());

  std::string url = "test url string";
  auto app_config = AppConfig::New();
  app_config->url = url;

  auth::TokenProviderFactoryPtr token_provider_factory_ptr;
  auto token_provider_factory_request = token_provider_factory_ptr.NewRequest();

  UserControllerPtr user_controller_ptr;
  UserControllerImpl impl(
      application_launcher_ptr.get(), app_config.Clone(), app_config.Clone(),
      app_config.Clone(), std::move(token_provider_factory_ptr),
      nullptr /* account */, nullptr /* view_owner_request */,
      user_controller_ptr.NewRequest(), nullptr /* done_callback */);

  EXPECT_TRUE(RunLoopUntil([&test_application_launcher] {
    return !test_application_launcher.last_launch_info.is_null();
  }));

  EXPECT_FALSE(test_application_launcher.last_launch_info.is_null());
  EXPECT_EQ(url, test_application_launcher.last_launch_info->url);
}

}  // namespace
}  // namespace modular
