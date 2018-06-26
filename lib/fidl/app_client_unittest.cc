// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/fidl/app_client.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <test/peridot/lib/fidl/appclient/cpp/fidl.h>

#include "gtest/gtest.h"
#include "lib/gtest/test_loop_fixture.h"
#include "peridot/lib/testing/fake_launcher.h"

namespace modular {
namespace testing {
namespace {

using ::test::peridot::lib::fidl::appclient::TerminateService;

constexpr char kServiceName[] = "service1";
constexpr char kTestUrl[] = "some/test/url";

fuchsia::modular::AppConfig GetTestAppConfig() {
  fuchsia::modular::AppConfig app_config;
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

class AppClientTest : public gtest::TestLoopFixture {};

TEST_F(AppClientTest, BaseRun_Success) {
  bool callback_called = false;
  FakeLauncher launcher;
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
  FakeLauncher launcher;
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
  RunLoopUntilIdle();
  EXPECT_TRUE(app_terminated_callback_called);
  EXPECT_TRUE(controller.killed());
}

TEST_F(AppClientTest, Run_Success) {
  bool callback_called = false;
  FakeLauncher launcher;
  launcher.RegisterApplication(
      kTestUrl,
      [&callback_called](
          fuchsia::sys::LaunchInfo launch_info,
          fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        EXPECT_EQ(kTestUrl, launch_info.url);
        callback_called = true;
      });

  AppClient<TerminateService> app_client(&launcher, GetTestAppConfig());

  EXPECT_TRUE(callback_called);
}

TEST_F(AppClientTest, RunWithParams_Success) {
  fuchsia::sys::ServiceListPtr additional_services =
      fuchsia::sys::ServiceList::New();
  additional_services->names.push_back(kServiceName);
  // We just need |provider_request| to stay around till the end of this test.
  auto provider_request = additional_services->provider.NewRequest();

  bool callback_called = false;
  FakeLauncher launcher;
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

  AppClient<TerminateService> app_client(&launcher, GetTestAppConfig(), "",
                                         std::move(additional_services));

  EXPECT_TRUE(callback_called);
}

}  // namespace
}  // namespace testing
}  // namespace modular
