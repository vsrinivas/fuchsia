// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/fidl/app_client.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <modular_test/cpp/fidl.h>
#include "gtest/gtest.h"
#include "lib/gtest/test_with_message_loop.h"
#include "peridot/lib/testing/fake_application_launcher.h"

namespace fuchsia {
namespace modular {
namespace testing {
namespace {

constexpr char kServiceName[] = "service1";
constexpr char kTestUrl[] = "some/test/url";

AppConfig GetTestAppConfig() {
  AppConfig app_config;
  app_config.url = kTestUrl;
  return app_config;
}

class TestComponentController : fuchsia::sys::ComponentController {
 public:
  TestComponentController() : binding_(this) {}

  void Connect(
      fidl::InterfaceRequest<fuchsia::sys::ComponentController> request) {
    binding_.Bind(std::move(request));
    binding_.set_error_handler([this] { Kill(); });
  }

  bool killed() { return killed_; }

 private:
  void Kill() override { killed_ = true; }

  void Detach() override {}

  void Wait(WaitCallback callback) override {}

  fidl::Binding<fuchsia::sys::ComponentController> binding_;

  bool killed_{};

  FXL_DISALLOW_COPY_AND_ASSIGN(TestComponentController);
};

class AppClientTest : public gtest::TestWithMessageLoop {};

TEST_F(AppClientTest, BaseRun_Success) {
  bool callback_called = false;
  FakeApplicationLauncher launcher;
  launcher.RegisterApplication(
      kTestUrl,
      [&callback_called](
          fuchsia::sys::LaunchInfo launch_info,
          fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        EXPECT_EQ(kTestUrl, launch_info.url);
        callback_called = true;
      });
  AppClientBase app_client_base(&launcher, GetTestAppConfig());

  EXPECT_TRUE(callback_called);
}

TEST_F(AppClientTest, BaseTerminate_Success) {
  FakeApplicationLauncher launcher;
  TestComponentController controller;
  bool callback_called = false;
  launcher.RegisterApplication(
      kTestUrl,
      [&callback_called, &controller](
          fuchsia::sys::LaunchInfo launch_info,
          fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        EXPECT_EQ(kTestUrl, launch_info.url);
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
  EXPECT_TRUE(
      RunLoopUntilWithTimeout([&app_terminated_callback_called, &controller] {
        return app_terminated_callback_called && controller.killed();
      }));

  EXPECT_TRUE(controller.killed());
}

TEST_F(AppClientTest, Run_Success) {
  bool callback_called = false;
  FakeApplicationLauncher launcher;
  launcher.RegisterApplication(
      kTestUrl,
      [&callback_called](
          fuchsia::sys::LaunchInfo launch_info,
          fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        EXPECT_EQ(kTestUrl, launch_info.url);
        callback_called = true;
      });

  AppClient<modular_test::TerminateService> app_client(&launcher,
                                                       GetTestAppConfig());

  EXPECT_TRUE(callback_called);
}

TEST_F(AppClientTest, RunWithParams_Success) {
  fuchsia::sys::ServiceListPtr additional_services = fuchsia::sys::ServiceList::New();
  additional_services->names.push_back(kServiceName);
  // We just need |provider_request| to stay around till the end of this test.
  auto provider_request = additional_services->provider.NewRequest();

  bool callback_called = false;
  FakeApplicationLauncher launcher;
  launcher.RegisterApplication(
      kTestUrl,
      [&callback_called](
          fuchsia::sys::LaunchInfo launch_info,
          fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        EXPECT_EQ(kTestUrl, launch_info.url);
        auto additional_services = std::move(launch_info.additional_services);
        EXPECT_EQ(kServiceName, additional_services->names->at(0));
        callback_called = true;
      });

  AppClient<modular_test::TerminateService> app_client(
      &launcher, GetTestAppConfig(), "", std::move(additional_services));

  EXPECT_TRUE(callback_called);
}

}  // namespace
}  // namespace testing
}  // namespace modular
}  // namespace fuchsia
