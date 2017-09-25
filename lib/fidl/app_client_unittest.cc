// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/fidl/app_client.h"
#include "gtest/gtest.h"
#include "lib/app/fidl/application_controller.fidl.h"
#include "lib/app/fidl/application_launcher.fidl.h"
#include "peridot/lib/fidl/app_client_unittest.fidl.h"
#include "peridot/lib/testing/test_with_message_loop.h"

namespace modular {
namespace {

constexpr char kTestUrl[] = "some/test/url";

AppConfigPtr GetTestAppConfig() {
  auto app_config = AppConfig::New();
  app_config->url = kTestUrl;
  return app_config;
}

class TestApplicationController : app::ApplicationController {
 public:
  TestApplicationController() : binding_(this) {}

  void Connect(fidl::InterfaceRequest<app::ApplicationController> request) {
    binding_.Bind(std::move(request));
    binding_.set_connection_error_handler([this] { Kill(); });
  }

  bool killed() { return killed_; }

 private:
  void Kill() override { killed_ = true; }

  void Detach() override {}

  void Wait(const WaitCallback& callback) override {}

  fidl::Binding<app::ApplicationController> binding_;

  bool killed_{};

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApplicationController);
};

// TODO(alhaad): Factor out this use into a common fake-application-launcher.
class TestApplicationLauncher : app::ApplicationLauncher {
 public:
  explicit TestApplicationLauncher(
      fidl::InterfaceRequest<app::ApplicationLauncher> request)
      : binding_(this, std::move(request)) {}

  app::ApplicationLaunchInfoPtr last_launch_info;
  TestApplicationController controller;

 private:
  void CreateApplication(
      app::ApplicationLaunchInfoPtr launch_info,
      fidl::InterfaceRequest<app::ApplicationController> request) override {
    last_launch_info = std::move(launch_info);
    controller.Connect(std::move(request));
  }

  fidl::Binding<app::ApplicationLauncher> binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApplicationLauncher);
};

class AppClientTest : public testing::TestWithMessageLoop {};

TEST_F(AppClientTest, BaseRun_Success) {
  app::ApplicationLauncherPtr application_launcher_ptr;
  TestApplicationLauncher test_application_launcher(
      application_launcher_ptr.NewRequest());

  AppClientBase app_client_base(application_launcher_ptr.get(),
                                GetTestAppConfig());

  EXPECT_TRUE(RunLoopUntil([&test_application_launcher] {
    return !test_application_launcher.last_launch_info.is_null();
  }));

  EXPECT_EQ(kTestUrl, test_application_launcher.last_launch_info->url);
}

TEST_F(AppClientTest, BaseTerminate_Success) {
  app::ApplicationLauncherPtr application_launcher_ptr;
  TestApplicationLauncher test_application_launcher(
      application_launcher_ptr.NewRequest());

  AppClientBase app_client_base(application_launcher_ptr.get(),
                                GetTestAppConfig());

  bool app_terminated_callback_called = false;
  app_client_base.Teardown(fxl::TimeDelta::Zero(),
                           [&app_terminated_callback_called] {
                             app_terminated_callback_called = true;
                           });

  EXPECT_TRUE(RunLoopUntil(
      [&app_terminated_callback_called, &test_application_launcher] {
        return app_terminated_callback_called &&
               test_application_launcher.controller.killed();
      }));

  EXPECT_FALSE(test_application_launcher.last_launch_info.is_null());
  EXPECT_EQ(kTestUrl, test_application_launcher.last_launch_info->url);
  EXPECT_TRUE(test_application_launcher.controller.killed());
}

TEST_F(AppClientTest, Run_Success) {
  app::ApplicationLauncherPtr application_launcher_ptr;
  TestApplicationLauncher test_application_launcher(
      application_launcher_ptr.NewRequest());

  AppClient<test::TerminateService> app_client(application_launcher_ptr.get(),
                                               GetTestAppConfig());

  EXPECT_TRUE(RunLoopUntil([&test_application_launcher] {
    return !test_application_launcher.last_launch_info.is_null();
  }));

  EXPECT_EQ(kTestUrl, test_application_launcher.last_launch_info->url);
}

}  // namespace
}  // namespace modular
