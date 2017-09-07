// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/fidl/app_client.h"

#include "gtest/gtest.h"
#include "lib/app/fidl/application_controller.fidl.h"
#include "lib/app/fidl/application_launcher.fidl.h"
#include "peridot/lib/fidl/app_client_unittest.fidl.h"
#include "peridot/lib/testing/fake_application_launcher.h"
#include "peridot/lib/testing/test_with_message_loop.h"

namespace modular {
namespace testing {
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

class AppClientTest : public testing::TestWithMessageLoop {};

TEST_F(AppClientTest, BaseRun_Success) {
  bool callback_called = false;
  FakeApplicationLauncher launcher;
  launcher.RegisterApplication(
      kTestUrl, [&callback_called](
                    app::ApplicationLaunchInfoPtr launch_info,
                    fidl::InterfaceRequest<app::ApplicationController> ctrl) {
        EXPECT_EQ(kTestUrl, launch_info->url);
        callback_called = true;
      });
  AppClientBase app_client_base(&launcher, GetTestAppConfig());

  EXPECT_TRUE(callback_called);
}

TEST_F(AppClientTest, BaseTerminate_Success) {
  FakeApplicationLauncher launcher;
  TestApplicationController controller;
  bool callback_called = false;
  launcher.RegisterApplication(
      kTestUrl, [&callback_called, &controller](
                    app::ApplicationLaunchInfoPtr launch_info,
                    fidl::InterfaceRequest<app::ApplicationController> ctrl) {
        EXPECT_EQ(kTestUrl, launch_info->url);
        callback_called = true;
        controller.Connect(std::move(ctrl));
      });

  AppClientBase app_client_base(&launcher, GetTestAppConfig());

  bool app_terminated_callback_called = false;
  app_client_base.Teardown(fxl::TimeDelta::Zero(),
                           [&app_terminated_callback_called] {
                             app_terminated_callback_called = true;
                           });

  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(RunLoopUntil([&app_terminated_callback_called, &controller] {
    return app_terminated_callback_called && controller.killed();
  }));

  EXPECT_TRUE(controller.killed());
}

TEST_F(AppClientTest, Run_Success) {
  bool callback_called = false;
  FakeApplicationLauncher launcher;
  launcher.RegisterApplication(
      kTestUrl, [&callback_called](
                    app::ApplicationLaunchInfoPtr launch_info,
                    fidl::InterfaceRequest<app::ApplicationController> ctrl) {
        EXPECT_EQ(kTestUrl, launch_info->url);
        callback_called = true;
      });

  AppClient<test::TerminateService> app_client(&launcher, GetTestAppConfig());

  EXPECT_TRUE(callback_called);
}

}  // namespace
}  // namespace testing
}  // namespace modular
