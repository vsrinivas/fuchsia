// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <glob.h>

#include <fidl/examples/echo/cpp/fidl.h>

#include "garnet/bin/appmgr/integration_tests/mock_runner_registry.h"
#include "lib/component/cpp/testing/test_with_environment.h"
#include "lib/fxl/files/path.h"
#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/strings/string_printf.h"

namespace component {
namespace {

using fuchsia::sys::TerminationReason;
using test::component::mockrunner::MockComponentPtr;
using testing::EnclosingEnvironment;
using testing::MockRunnerRegistry;
using testing::TestWithEnvironment;

const char kRealm[] = "realmrunnerintegrationtest";
const auto kTimeout = zx::sec(5);
const char kComponentForRunner[] = "fake_component_for_runner";
const std::string kComponentForRunnerUrl =
    std::string("file://") + kComponentForRunner;

class RealmRunnerTest : public TestWithEnvironment {
 protected:
  void SetUp() override {
    TestWithEnvironment::SetUp();
    enclosing_environment_ = CreateNewEnclosingEnvironment(kRealm);
    enclosing_environment_->AddService(runner_registry_.GetHandler());
    ASSERT_TRUE(WaitForEnclosingEnvToStart(enclosing_environment_.get()));
  }

  bool WaitForRunnerToRegister() {
    const bool ret = RunLoopWithTimeoutOrUntil(
        [&] { return runner_registry_.runner(); }, kTimeout);
    EXPECT_TRUE(ret) << "Waiting for connection timed out: "
                     << runner_registry_.connect_count();
    return ret;
  }

  fuchsia::sys::LaunchInfo CreateLaunchInfo(const std::string& url) {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = url;
    return launch_info;
  }

  bool WaitForRunnerToDie() {
    const bool ret = RunLoopWithTimeoutOrUntil(
        [&] { return !runner_registry_.runner(); }, kTimeout);
    EXPECT_TRUE(ret) << "Waiting for connection timed out: "
                     << runner_registry_.dead_runner_count();
    return ret;
  }

  bool WaitForComponentCount(size_t expected_components_count) {
    auto runner = runner_registry_.runner();
    const bool ret = RunLoopWithTimeoutOrUntil(
        [&] {
          return runner->components().size() == expected_components_count;
        },
        kTimeout);
    EXPECT_TRUE(ret) << "Waiting for component to start/die timed out, got:"
                     << runner->components().size()
                     << ", expected: " << expected_components_count;
    return ret;
  }

  std::unique_ptr<EnclosingEnvironment> enclosing_environment_;
  MockRunnerRegistry runner_registry_;
};

TEST_F(RealmRunnerTest, RunnerLaunched) {
  auto component =
      enclosing_environment_->CreateComponentFromUrl(kComponentForRunner);
  ASSERT_TRUE(WaitForRunnerToRegister());
  ASSERT_TRUE(WaitForComponentCount(1));
  auto components = runner_registry_.runner()->components();
  ASSERT_EQ(components[0].url, kComponentForRunnerUrl);
}

TEST_F(RealmRunnerTest, RunnerLaunchedOnlyOnce) {
  auto component1 =
      enclosing_environment_->CreateComponentFromUrl(kComponentForRunner);
  ASSERT_TRUE(WaitForRunnerToRegister());
  // launch again and check that runner was not executed again
  auto component2 =
      enclosing_environment_->CreateComponentFromUrl(kComponentForRunner);

  WaitForComponentCount(2);
  EXPECT_EQ(1, runner_registry_.connect_count());
}

TEST_F(RealmRunnerTest, RunnerLaunchedAgainWhenKilled) {
  auto component =
      enclosing_environment_->CreateComponentFromUrl(kComponentForRunner);
  ASSERT_TRUE(WaitForRunnerToRegister());
  int64_t return_code = INT64_MIN;
  component.events().OnTerminated =
      [&](int64_t code, TerminationReason reason) { return_code = code; };
  runner_registry_.runner()->runner_ptr()->Crash();
  ASSERT_TRUE(WaitForRunnerToDie());
  // make sure component is dead. This makes sure that runner was killed inside
  // appmgr.
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&] { return return_code != INT64_MIN; }, kTimeout));

  // launch again and check that runner was executed again
  component =
      enclosing_environment_->CreateComponentFromUrl(kComponentForRunner);
  ASSERT_TRUE(WaitForRunnerToRegister());
  ASSERT_EQ(2, runner_registry_.connect_count());
  // make sure component was also launched
  ASSERT_TRUE(WaitForComponentCount(1));
  auto components = runner_registry_.runner()->components();
  ASSERT_EQ(components[0].url, kComponentForRunnerUrl);
}

TEST_F(RealmRunnerTest, ComponentBridgeReturnsRightReturnCode) {
  auto component =
      enclosing_environment_->CreateComponentFromUrl(kComponentForRunner);
  ASSERT_TRUE(WaitForRunnerToRegister());
  // make sure component was launched
  ASSERT_TRUE(WaitForComponentCount(1));
  int64_t return_code;
  TerminationReason reason;
  component.events().OnTerminated = [&](int64_t code, TerminationReason r) {
    return_code = code;
    reason = r;
  };
  auto components = runner_registry_.runner()->components();
  const int64_t ret_code = 3;
  MockComponentPtr component_ptr;
  runner_registry_.runner()->runner_ptr()->ConnectToComponent(
      components[0].unique_id, component_ptr.NewRequest());
  component_ptr->Kill(ret_code);
  ASSERT_TRUE(WaitForComponentCount(0));
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil(
      [&] { return reason == TerminationReason::EXITED; }, kTimeout));
  EXPECT_EQ(return_code, ret_code);
}

TEST_F(RealmRunnerTest, DestroyingControllerKillsComponent) {
  {
    auto component =
        enclosing_environment_->CreateComponentFromUrl(kComponentForRunner);
    ASSERT_TRUE(WaitForRunnerToRegister());
    // make sure component was launched
    ASSERT_TRUE(WaitForComponentCount(1));
    // component will go out of scope
  }
  ASSERT_TRUE(WaitForComponentCount(0));
}

TEST_F(RealmRunnerTest, KillComponentController) {
  auto component =
      enclosing_environment_->CreateComponentFromUrl(kComponentForRunner);
  ASSERT_TRUE(WaitForRunnerToRegister());
  // make sure component was launched
  ASSERT_TRUE(WaitForComponentCount(1));
  TerminationReason reason;
  component.events().OnTerminated = [&](int64_t code, TerminationReason r) {
    reason = r;
  };
  component->Kill();
  ASSERT_TRUE(WaitForComponentCount(0));
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil(
      [&] { return reason == TerminationReason::EXITED; }, kTimeout));
}

TEST_F(RealmRunnerTest, ComponentCanConnectToEnvService) {
  ASSERT_EQ(ZX_OK, enclosing_environment_->AddServiceWithLaunchInfo(
                       CreateLaunchInfo("echo2_server_cpp"),
                       fidl::examples::echo::Echo::Name_));
  auto component =
      enclosing_environment_->CreateComponentFromUrl(kComponentForRunner);
  ASSERT_TRUE(WaitForRunnerToRegister());
  // make sure component was launched
  ASSERT_TRUE(WaitForComponentCount(1));

  fidl::examples::echo::EchoPtr echo;
  MockComponentPtr component_ptr;
  runner_registry_.runner()->runner_ptr()->ConnectToComponent(
      runner_registry_.runner()->components()[0].unique_id,
      component_ptr.NewRequest());
  component_ptr->ConnectToService(fidl::examples::echo::Echo::Name_,
                                  echo.NewRequest().TakeChannel());
  const std::string message = "ConnectToEnvService";
  fidl::StringPtr ret_msg = "";
  echo->EchoString(message,
                   [&](::fidl::StringPtr retval) { ret_msg = retval; });
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&] { return ret_msg.get() == message; }, kTimeout));
}

TEST_F(RealmRunnerTest, ProbeHub) {
  auto glob_str =
      fxl::StringPrintf("/hub/r/sys/*/r/%s/*/c/appmgr_mock_runner/*/c/%s/*",
                        kRealm, kComponentForRunner);
  glob_t globbuf;
  // launch two components and make sure both show up in /hub.
  auto component1 =
      enclosing_environment_->CreateComponentFromUrl(kComponentForRunner);
  auto component2 =
      enclosing_environment_->CreateComponentFromUrl(kComponentForRunner);
  ASSERT_TRUE(WaitForRunnerToRegister());
  WaitForComponentCount(2);
  ASSERT_EQ(glob(glob_str.data(), 0, nullptr, &globbuf), 0)
      << glob_str << " does not exist.";

  auto guard = fxl::MakeAutoCall([&]() { globfree(&globbuf); });

  ASSERT_EQ(globbuf.gl_pathc, 2u);

  const std::string path1 = globbuf.gl_pathv[0];
  const std::string path2 = globbuf.gl_pathv[1];
  EXPECT_NE(path1, path2);
  EXPECT_EQ(files::GetDirectoryName(path1), files::GetDirectoryName(path2));
}

}  // namespace
}  // namespace component
