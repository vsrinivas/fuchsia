// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/fidl/app_client.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/fake_launcher.h>

#include <gtest/gtest.h>
#include <test/peridot/lib/fidl/appclient/cpp/fidl.h>

namespace modular_testing {
namespace {

using ::sys::testing::FakeLauncher;
using ::test::peridot::lib::fidl::appclient::TerminateService;

constexpr char kServiceName[] = "service1";
constexpr char kTestUrl[] = "some/test/url";

fuchsia::modular::session::AppConfig GetTestAppConfig() {
  fuchsia::modular::session::AppConfig app_config;
  app_config.set_url(kTestUrl);
  return app_config;
}

class TestComponentController : fuchsia::sys::ComponentController {
 public:
  TestComponentController() : binding_(this) {}

  void Connect(fidl::InterfaceRequest<fuchsia::sys::ComponentController> request) {
    binding_.Bind(std::move(request));
    binding_.set_error_handler([this](zx_status_t status) { Kill(); });
  }

  bool killed() { return killed_; }

 private:
  void Kill() override { killed_ = true; }

  void Detach() override {}

  fidl::Binding<fuchsia::sys::ComponentController> binding_;

  bool killed_{};

  FXL_DISALLOW_COPY_AND_ASSIGN(TestComponentController);
};

class AppClientTest : public gtest::TestLoopFixture {};

TEST_F(AppClientTest, BaseRun_Success) {
  bool callback_called = false;
  FakeLauncher launcher;
  launcher.RegisterComponent(
      kTestUrl, [&callback_called](fuchsia::sys::LaunchInfo launch_info,
                                   fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        EXPECT_EQ(kTestUrl, launch_info.url);
        callback_called = true;
      });
  modular::AppClientBase app_client_base(&launcher, GetTestAppConfig());

  EXPECT_TRUE(callback_called);
}

TEST_F(AppClientTest, BaseTerminate_Success) {
  FakeLauncher launcher;
  TestComponentController controller;
  bool callback_called = false;
  launcher.RegisterComponent(kTestUrl,
                             [&callback_called, &controller](
                                 fuchsia::sys::LaunchInfo launch_info,
                                 fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
                               EXPECT_EQ(kTestUrl, launch_info.url);
                               callback_called = true;
                               controller.Connect(std::move(ctrl));
                             });

  modular::AppClientBase app_client_base(&launcher, GetTestAppConfig());

  bool app_terminated_callback_called = false;
  app_client_base.Teardown(
      zx::duration(), [&app_terminated_callback_called] { app_terminated_callback_called = true; });

  EXPECT_TRUE(callback_called);
  RunLoopUntilIdle();
  EXPECT_TRUE(app_terminated_callback_called);
  EXPECT_TRUE(controller.killed());
}

TEST_F(AppClientTest, Run_Success) {
  bool callback_called = false;
  FakeLauncher launcher;
  launcher.RegisterComponent(
      kTestUrl, [&callback_called](fuchsia::sys::LaunchInfo launch_info,
                                   fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        EXPECT_EQ(kTestUrl, launch_info.url);
        callback_called = true;
      });

  modular::AppClient<TerminateService> app_client(&launcher, GetTestAppConfig());

  EXPECT_TRUE(callback_called);
}

TEST_F(AppClientTest, RunWithParams_Success) {
  fuchsia::sys::ServiceListPtr additional_services = std::make_unique<fuchsia::sys::ServiceList>();
  additional_services->names.push_back(kServiceName);
  // We just need |provider_request| to stay around till the end of this test.
  auto provider_request = additional_services->provider.NewRequest();

  bool callback_called = false;
  FakeLauncher launcher;
  launcher.RegisterComponent(
      kTestUrl, [&callback_called](fuchsia::sys::LaunchInfo launch_info,
                                   fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        EXPECT_EQ(kTestUrl, launch_info.url);
        auto additional_services = std::move(launch_info.additional_services);
        EXPECT_EQ(kServiceName, additional_services->names.at(0));
        callback_called = true;
      });

  modular::AppClient<TerminateService> app_client(&launcher, GetTestAppConfig(), "",
                                                  std::move(additional_services));

  EXPECT_TRUE(callback_called);
}

}  // namespace
}  // namespace modular_testing
