// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/device_runner/user_controller_impl.h"

#include "gtest/gtest.h"
#include "lib/app/fidl/application_launcher.fidl.h"
#include "peridot/lib/testing/fake_application_launcher.h"
#include "peridot/lib/testing/test_with_message_loop.h"

namespace modular {
namespace testing {
namespace {

class UserControllerImplTest : public testing::TestWithMessageLoop {};

TEST_F(UserControllerImplTest, StartUserRunner) {
  FakeApplicationLauncher launcher;
  std::string url = "test_url_string";
  auto app_config = AppConfig::New();
  app_config->url = url;

  auth::TokenProviderFactoryPtr token_provider_factory_ptr;
  auto token_provider_factory_request = token_provider_factory_ptr.NewRequest();

  bool callback_called = false;
  launcher.RegisterApplication(
      url, [&callback_called](
               app::ApplicationLaunchInfoPtr launch_info,
               fidl::InterfaceRequest<app::ApplicationController> ctrl) {
        callback_called = true;
      });

  UserControllerPtr user_controller_ptr;
  UserControllerImpl impl(
      &launcher, app_config.Clone(), app_config.Clone(), app_config.Clone(),
      std::move(token_provider_factory_ptr), nullptr /* account */,
      nullptr /* view_owner_request */, nullptr /* device_shell_services */,
      user_controller_ptr.NewRequest(), nullptr /* done_callback */);

  EXPECT_TRUE(callback_called);
}

}  // namespace
}  // namespace testing
}  // namespace modular
