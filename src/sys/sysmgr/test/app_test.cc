// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/sysmgr/app.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/fidl/cpp/string.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/component_interceptor.h>

#include <gtest/gtest.h>

#include "src/lib/files/file.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/strings/substitute.h"

namespace sysmgr {
namespace {

class TestSysmgr : public ::testing::Test {
 protected:
  // Makes a Config object with contents filled with |json| and returns its paths.
  // This method should only be called once in a test case.
  ::sysmgr::Config NewConfig(const std::string& json) {
    std::string tmp_dir_path;
    tmp_dir_.NewTempDir(&tmp_dir_path);
    const std::string json_file = fxl::Substitute("$0/sysmgr.config", tmp_dir_path);
    ZX_ASSERT(files::WriteFile(json_file, json.data(), json.size()));
    Config config;
    config.ParseFromDirectory(tmp_dir_path);
    ZX_ASSERT(!config.HasError());
    return config;
  }

  files::ScopedTempDir tmp_dir_;
};

// Smoke test that LaunchComponent doesn't crash.
TEST_F(TestSysmgr, LaunchComponent) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  ::sysmgr::Config config = NewConfig(R"({})");
  auto component_context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  ::sysmgr::App app(std::move(config), component_context->svc(), &loop);

  auto launch_info =
      fuchsia::sys::LaunchInfo{.url = "fuchsia-pkg://example.com/pkg#meta/component.cmx"};
  app.LaunchComponent(std::move(launch_info), nullptr, nullptr);
}

// Test that a critical component crashing too many times results in sysmgr exiting.
// This is accomplished by configuring a component which doesn't exist as a critical component.
TEST_F(TestSysmgr, LaunchCrashingCriticalComponent) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  ::sysmgr::Config config = NewConfig(R"({
    "services": {
      "example.random.service": "fuchsia-pkg://example.com/pkg#meta/component.cmx"
    },
    "startup_services": ["example.random.service"],
    "critical_components": ["fuchsia-pkg://example.com/pkg#meta/component.cmx"]
  })");
  auto component_context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  ::sysmgr::App app(std::move(config), component_context->svc(), &loop);

  auto launch_info =
      fuchsia::sys::LaunchInfo{.url = "fuchsia-pkg://example.com/pkg#meta/component.cmx"};
  loop.Run();
  // Here, the component should launch because it provides a startup_service.
  // Because the component URL doesn't exist, sysmgr should exit its loop from faling to launch
  // repeatedly.
}

TEST_F(TestSysmgr, MaxRetries) {
  constexpr char kFakeUrl[] = "fuchsia-pkg://example.com/pkg#meta/component.cmx";
  constexpr size_t kExpectedMaxRetries = 3;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  // component.cmx doesn't launch on startup from this point.
  ::sysmgr::Config config = NewConfig(R"({
    "services": {
      "example.random.service": "fuchsia-pkg://example.com/pkg#meta/component.cmx"
    },
    "startup_services": ["example.random.service"],
    "critical_components": ["fuchsia-pkg://example.com/pkg#meta/component.cmx"]
  })");
  auto component_context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  auto incoming_svc = component_context->svc();

  auto interceptor = ::sys::testing::ComponentInterceptor::CreateWithEnvironmentLoader(
      incoming_svc->Connect<fuchsia::sys::Environment>(), loop.dispatcher());
  auto env = ::sys::testing::EnclosingEnvironment::Create(
      "test_harness", incoming_svc->Connect<fuchsia::sys::Environment>(),
      interceptor.MakeEnvironmentServices(incoming_svc->Connect<fuchsia::sys::Environment>()));

  size_t num_launches = 0;
  ASSERT_TRUE(interceptor.InterceptURL(
      kFakeUrl, "",
      [&num_launches](fuchsia::sys::StartupInfo startup_info,
                      std::unique_ptr<sys::testing::InterceptedComponent> component) {
        num_launches++;
        // immediately exit the component.
      }));

  ::sysmgr::App app(std::move(config), env->service_directory(), &loop);
  loop.Run();
  // sysmgr exits the loop from failing to restart |kFakeUrl| repeatedly.
  EXPECT_EQ(kExpectedMaxRetries, num_launches - 1);
}

}  // namespace
}  // namespace sysmgr
