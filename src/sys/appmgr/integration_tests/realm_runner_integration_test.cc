// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/test_with_environment_fixture.h>
#include <unistd.h>

#include <memory>
#include <sstream>
#include <vector>

#include <fidl/examples/echo/cpp/fidl.h>
#include <gmock/gmock.h>

#include "fuchsia/sys/cpp/fidl.h"
#include "lib/async-loop/cpp/loop.h"
#include "lib/async-loop/default.h"
#include "lib/async-loop/loop.h"
#include "lib/fidl/internal.h"
#include "src/lib/files/glob.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/sys/appmgr/integration_tests/mock_runner_registry.h"

namespace component {
namespace {

using fuchsia::sys::TerminationReason;
using gtest::TestWithEnvironmentFixture;
using sys::testing::EnclosingEnvironment;
using test::component::mockrunner::MockComponentPtr;
using test::component::mockrunner::MockComponentSyncPtr;
using testing::MockRunnerRegistry;

const char kRealm[] = "realmrunnerintegrationtest";
const char kComponentForRunner[] =
    "fuchsia-pkg://fuchsia.com/fake_component_for_runner#meta/"
    "fake_component_for_runner.cmx";
const char kComponentForRunnerProcessName[] = "fake_component_for_runner.cmx";
const char kNestedEnvLabel[] = "nested-environment";

class RealmRunnerTest : public TestWithEnvironmentFixture {
 protected:
  void SetUp() override {
    TestWithEnvironmentFixture::SetUp();
    auto services = CreateServices();
    ASSERT_EQ(ZX_OK, services->AddService(runner_registry_.GetHandler()));
    enclosing_environment_ = CreateNewEnclosingEnvironment(kRealm, std::move(services));
    WaitForEnclosingEnvToStart(enclosing_environment_.get());
  }

  std::pair<std::unique_ptr<EnclosingEnvironment>, std::unique_ptr<MockRunnerRegistry>>
  MakeNestedEnvironment(const fuchsia::sys::EnvironmentOptions& options,
                        EnclosingEnvironment* parent_env = nullptr,
                        std::string env_label = kNestedEnvLabel) {
    fuchsia::sys::EnvironmentPtr env;
    if (!parent_env) {
      parent_env = enclosing_environment_.get();
    }
    parent_env->ConnectToService(fuchsia::sys::Environment::Name_, env.NewRequest().TakeChannel());
    auto registry = std::make_unique<MockRunnerRegistry>();
    auto services = sys::testing::EnvironmentServices::Create(env);
    EXPECT_EQ(ZX_OK, services->AddService(registry->GetHandler()));
    auto nested_environment =
        EnclosingEnvironment::Create(env_label, env, std::move(services), std::move(options));
    WaitForEnclosingEnvToStart(nested_environment.get());
    return std::make_pair(std::move(nested_environment), std::move(registry));
  }

  void WaitForRunnerToRegister(MockRunnerRegistry* runner_registry = nullptr) {
    if (!runner_registry) {
      runner_registry = &runner_registry_;
    }
    RunLoopUntil([&] { return runner_registry->runner(); });
  }

  fuchsia::sys::LaunchInfo CreateLaunchInfo(const std::string& url) {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = url;
    return launch_info;
  }

  void WaitForRunnerToDie() {
    RunLoopUntil([&] { return !runner_registry_.runner(); });
  }

  void WaitForComponentCount(size_t expected_components_count) {
    return WaitForComponentCount(&runner_registry_, expected_components_count);
  }

  void WaitForComponentCount(MockRunnerRegistry* runner_registry,
                             size_t expected_components_count) {
    auto runner = runner_registry->runner();
    RunLoopUntil([&] { return runner->components().size() == expected_components_count; });
  }

  std::unique_ptr<EnclosingEnvironment> enclosing_environment_;
  MockRunnerRegistry runner_registry_;
};

TEST_F(RealmRunnerTest, RunnerLaunched) {
  auto component = enclosing_environment_->CreateComponentFromUrl(kComponentForRunner);
  WaitForRunnerToRegister();
  WaitForComponentCount(1);
  auto components = runner_registry_.runner()->components();
  ASSERT_EQ(components[0].url, kComponentForRunner);
}

TEST_F(RealmRunnerTest, RunnerLaunchedOnlyOnce) {
  auto component1 = enclosing_environment_->CreateComponentFromUrl(kComponentForRunner);
  WaitForRunnerToRegister();
  // launch again and check that runner was not executed again
  auto component2 = enclosing_environment_->CreateComponentFromUrl(kComponentForRunner);

  WaitForComponentCount(2);
  EXPECT_EQ(1, runner_registry_.connect_count());
}

TEST_F(RealmRunnerTest, RunnerLaunchedAgainWhenKilled) {
  auto component = enclosing_environment_->CreateComponentFromUrl(kComponentForRunner);
  WaitForRunnerToRegister();

  auto glob_str = fxl::StringPrintf("/hub/r/%s/*/c/appmgr_mock_runner.cmx/*", kRealm);
  files::Glob glob(glob_str);
  ASSERT_EQ(glob.size(), 1u);
  std::string runner_path_in_hub = *(glob.begin());

  int64_t return_code = INT64_MIN;
  component.events().OnTerminated = [&](int64_t code, TerminationReason reason) {
    return_code = code;
  };
  runner_registry_.runner()->runner_ptr()->Crash();
  WaitForRunnerToDie();
  // Make sure component is dead.
  RunLoopUntil([&] { return return_code != INT64_MIN; });

  // Make sure we no longer have runner in hub. This will make sure that appmgr
  // knows that runner died, before we try to launch component again.
  RunLoopUntil([runner_path = runner_path_in_hub.c_str()] {
    struct stat s;
    return stat(runner_path, &s) != 0;
  });

  // launch again and check that runner was executed again
  component = enclosing_environment_->CreateComponentFromUrl(kComponentForRunner);
  WaitForRunnerToRegister();
  ASSERT_EQ(2, runner_registry_.connect_count());
  // make sure component was also launched
  WaitForComponentCount(1);
  auto components = runner_registry_.runner()->components();
  ASSERT_EQ(components[0].url, kComponentForRunner);
}

TEST_F(RealmRunnerTest, RunnerLaunchedForEachEnvironment) {
  auto component1 = enclosing_environment_->CreateComponentFromUrl(kComponentForRunner);
  WaitForRunnerToRegister();

  std::unique_ptr<EnclosingEnvironment> nested_environment;
  std::unique_ptr<MockRunnerRegistry> nested_registry;
  std::tie(nested_environment, nested_registry) = MakeNestedEnvironment({});

  // launch again and check that runner was created for the nested environment
  auto component2 = nested_environment->CreateComponentFromUrl(kComponentForRunner);
  WaitForRunnerToRegister(nested_registry.get());

  WaitForComponentCount(&runner_registry_, 1);
  WaitForComponentCount(nested_registry.get(), 1);
  EXPECT_EQ(1, runner_registry_.connect_count());
  EXPECT_EQ(1, nested_registry->connect_count());
}

TEST_F(RealmRunnerTest, RunnerSharedFromParent) {
  auto component1 = enclosing_environment_->CreateComponentFromUrl(kComponentForRunner);
  WaitForRunnerToRegister();

  std::unique_ptr<EnclosingEnvironment> nested_environment;
  std::unique_ptr<MockRunnerRegistry> nested_registry;
  {
    std::tie(nested_environment, nested_registry) =
        MakeNestedEnvironment({.use_parent_runners = true});
  }

  // launch again and check that the runner from the parent environment was
  // shared.
  auto component2 = nested_environment->CreateComponentFromUrl(kComponentForRunner);

  WaitForComponentCount(&runner_registry_, 2);
  EXPECT_EQ(1, runner_registry_.connect_count());
  EXPECT_EQ(0, nested_registry->connect_count());
}

TEST_F(RealmRunnerTest, RunnerStartedInParent) {
  std::unique_ptr<EnclosingEnvironment> nested_environment1;
  std::unique_ptr<MockRunnerRegistry> nested_registry1;
  {
    std::tie(nested_environment1, nested_registry1) =
        MakeNestedEnvironment({.use_parent_runners = true});
  }

  // Create a component and check that the runner was started in the parent env
  auto component1 = nested_environment1->CreateComponentFromUrl(kComponentForRunner);
  WaitForRunnerToRegister();

  WaitForComponentCount(&runner_registry_, 1);
  EXPECT_EQ(1, runner_registry_.connect_count());
  EXPECT_EQ(0, nested_registry1->connect_count());

  std::unique_ptr<EnclosingEnvironment> nested_environment2;
  std::unique_ptr<MockRunnerRegistry> nested_registry2;
  {
    std::tie(nested_environment2, nested_registry2) = MakeNestedEnvironment(
        {.use_parent_runners = true}, enclosing_environment_.get(), "nested-environment2");
  }

  // Create a second component and check that the runner was shared
  auto component2 = nested_environment2->CreateComponentFromUrl(kComponentForRunner);
  WaitForComponentCount(&runner_registry_, 2);
  EXPECT_EQ(1, runner_registry_.connect_count());
  EXPECT_EQ(0, nested_registry1->connect_count());
  EXPECT_EQ(0, nested_registry2->connect_count());
}

TEST_F(RealmRunnerTest, UseParentRunnerRecursive) {
  // Create first nested environment
  std::unique_ptr<EnclosingEnvironment> nested_environment;
  std::unique_ptr<MockRunnerRegistry> nested_registry;
  {
    std::tie(nested_environment, nested_registry) =
        MakeNestedEnvironment({.use_parent_runners = true});
  }

  // Create a nested environment within the nested environment
  std::unique_ptr<EnclosingEnvironment> double_nested_env;
  std::unique_ptr<MockRunnerRegistry> double_nested_registry;
  {
    std::tie(double_nested_env, double_nested_registry) = MakeNestedEnvironment(
        {.use_parent_runners = true}, nested_environment.get(), "double-nested-environment");
  }

  // Create a component in double-nested-environment
  auto component1 = double_nested_env->CreateComponentFromUrl(kComponentForRunner);
  WaitForRunnerToRegister();
  WaitForComponentCount(&runner_registry_, 1);

  // Check that the runner was started in enclosing_environment
  EXPECT_EQ(1, runner_registry_.connect_count());
  EXPECT_EQ(0, nested_registry->connect_count());
  EXPECT_EQ(0, double_nested_registry->connect_count());
}

TEST_F(RealmRunnerTest, ComponentBridgeReturnsRightReturnCode) {
  auto component = enclosing_environment_->CreateComponentFromUrl(kComponentForRunner);
  WaitForRunnerToRegister();
  // make sure component was launched
  WaitForComponentCount(1);
  int64_t return_code;
  TerminationReason reason;
  component.events().OnTerminated = [&](int64_t code, TerminationReason r) {
    return_code = code;
    reason = r;
  };
  auto components = runner_registry_.runner()->components();
  const int64_t ret_code = 3;
  MockComponentPtr component_ptr;
  runner_registry_.runner()->runner_ptr()->ConnectToComponent(components[0].unique_id,
                                                              component_ptr.NewRequest());
  component_ptr->Kill(ret_code);
  WaitForComponentCount(0);
  RunLoopUntil([&] { return reason == TerminationReason::EXITED; });
  EXPECT_EQ(return_code, ret_code);
}

TEST_F(RealmRunnerTest, DestroyingControllerKillsComponent) {
  {
    auto component = enclosing_environment_->CreateComponentFromUrl(kComponentForRunner);
    WaitForRunnerToRegister();
    // make sure component was launched
    WaitForComponentCount(1);
    // component will go out of scope
  }
  WaitForComponentCount(0);
}

TEST_F(RealmRunnerTest, KillComponentController) {
  auto component = enclosing_environment_->CreateComponentFromUrl(kComponentForRunner);
  WaitForRunnerToRegister();
  // make sure component was launched
  WaitForComponentCount(1);
  TerminationReason reason;
  component.events().OnTerminated = [&](int64_t code, TerminationReason r) { reason = r; };
  component->Kill();
  WaitForComponentCount(0);
  RunLoopUntil([&] { return reason == TerminationReason::EXITED; });
}

std::string ConvertToString(const std::vector<fuchsia::sys::ProgramMetadata>& vec) {
  if (vec.empty()) {
    return "empty vector of program metadata";
  }

  std::string ret = fxl::StringPrintf("%lu records:", vec.size());
  for (auto& metadata : vec) {
    ret +=
        fxl::StringPrintf("\n{key: %s, value: %s}", metadata.key.c_str(), metadata.value.c_str());
  }
  return ret;
}

TEST_F(RealmRunnerTest, ValidateProgramMetadata) {
  auto component = enclosing_environment_->CreateComponentFromUrl(kComponentForRunner);
  WaitForRunnerToRegister();
  // make sure component was launched
  WaitForComponentCount(1);

  auto components = runner_registry_.runner()->components();
  MockComponentSyncPtr component_ptr;
  runner_registry_.runner()->runner_ptr()->ConnectToComponent(components[0].unique_id,
                                                              component_ptr.NewRequest());

  std::vector<fuchsia::sys::ProgramMetadata> vec;
  ASSERT_EQ(ZX_OK, component_ptr->GetProgramMetadata(&vec));

  ASSERT_EQ(vec.size(), 3u) << ConvertToString(vec);

  auto data = vec[0];
  auto binary = vec[1];
  auto foobar_attribute = vec[2];

  EXPECT_EQ(data.key, "data");
  EXPECT_EQ(data.value, "data/fake_component_for_runner");

  EXPECT_EQ(binary.key, "binary");
  EXPECT_EQ(binary.value, "bin/fake_component");

  EXPECT_EQ(foobar_attribute.key, "foobar");
  EXPECT_EQ(foobar_attribute.value, "baz");
}

class RealmRunnerServiceTest : public RealmRunnerTest {
 protected:
  void SetUp() override {
    TestWithEnvironmentFixture::SetUp();
    auto env_services = CreateServices();
    ASSERT_EQ(ZX_OK, env_services->AddService(runner_registry_.GetHandler()));
    ASSERT_EQ(ZX_OK, env_services->AddServiceWithLaunchInfo(
                         CreateLaunchInfo("fuchsia-pkg://fuchsia.com/"
                                          "echo_server_cpp#meta/echo_server_cpp.cmx"),
                         fidl::examples::echo::Echo::Name_));
    enclosing_environment_ = CreateNewEnclosingEnvironment(kRealm, std::move(env_services));
  }
};

TEST_F(RealmRunnerServiceTest, ComponentCanConnectToEnvService) {
  auto component = enclosing_environment_->CreateComponentFromUrl(kComponentForRunner);
  WaitForRunnerToRegister();
  // make sure component was launched
  WaitForComponentCount(1);

  fidl::examples::echo::EchoPtr echo;
  MockComponentPtr component_ptr;
  runner_registry_.runner()->runner_ptr()->ConnectToComponent(
      runner_registry_.runner()->components()[0].unique_id, component_ptr.NewRequest());
  component_ptr->ConnectToService(fidl::examples::echo::Echo::Name_,
                                  echo.NewRequest().TakeChannel());
  const std::string message = "ConnectToEnvService";
  std::string ret_msg;
  echo->EchoString(message, [&](::fidl::StringPtr retval) { ret_msg = retval.value_or(""); });
  RunLoopUntil([&] { return ret_msg == message; });
}

TEST_F(RealmRunnerTest, ComponentCanPublishServices) {
  constexpr char dummy_service_name[] = "dummy_service";

  // launch component with service.
  zx::channel request;
  auto services = sys::ServiceDirectory::CreateWithRequest(&request);
  auto launch_info = CreateLaunchInfo(kComponentForRunner);
  launch_info.directory_request = std::move(request);
  auto component = enclosing_environment_->CreateComponent(std::move(launch_info));

  WaitForRunnerToRegister();
  // make sure component was launched
  WaitForComponentCount(1);

  // create and publish fake service
  vfs::PseudoDir fake_service_dir;
  bool connect_called = false;
  fake_service_dir.AddEntry(
      dummy_service_name,
      std::make_unique<vfs::Service>(
          [&](zx::channel channel, async_dispatcher_t* dispatcher) { connect_called = true; }));
  MockComponentSyncPtr component_ptr;
  runner_registry_.runner()->runner_ptr()->ConnectToComponent(
      runner_registry_.runner()->components()[0].unique_id, component_ptr.NewRequest());
  fidl::InterfaceHandle<fuchsia::io::Directory> dir_handle;
  fake_service_dir.Serve(fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
                         dir_handle.NewRequest().TakeChannel());
  ASSERT_EQ(ZX_OK, component_ptr->SetServiceDirectory(dir_handle.TakeChannel()));
  ASSERT_EQ(ZX_OK, component_ptr->PublishService(dummy_service_name));

  // try to connect to fake service
  fidl::examples::echo::EchoPtr echo;
  services->Connect(echo.NewRequest(), dummy_service_name);
  RunLoopUntil([&] { return connect_called; });
}

TEST_F(RealmRunnerTest, ProbeHub) {
  auto glob_str = fxl::StringPrintf("/hub/r/%s/*/c/appmgr_mock_runner.cmx/*/c/%s/*", kRealm,
                                    kComponentForRunnerProcessName);
  // launch two components and make sure both show up in /hub.
  auto component1 = enclosing_environment_->CreateComponentFromUrl(kComponentForRunner);
  auto component2 = enclosing_environment_->CreateComponentFromUrl(kComponentForRunner);
  WaitForRunnerToRegister();
  WaitForComponentCount(2);

  files::Glob glob(glob_str);
  ASSERT_EQ(glob.size(), 2u) << glob_str << " expected 2 matches.";

  std::vector<std::string> paths = {glob.begin(), glob.end()};
  EXPECT_NE(paths[0], paths[1]);

  // Verify that the pkg directory exists and can be enumerated.
  auto component_1_pkg_dir = fxl::StringPrintf("%s/in/pkg/*", paths[0].c_str());
  files::Glob pkg_dir_glob(component_1_pkg_dir);
  std::vector<std::string> pkg_dir_contents = {pkg_dir_glob.begin(), pkg_dir_glob.end()};
  ASSERT_EQ(pkg_dir_contents.size(), 1u) << "expected 1 entry in pkg";
  EXPECT_EQ("meta", files::GetBaseName(pkg_dir_contents[0]));

  EXPECT_EQ(files::GetDirectoryName(paths[0]), files::GetDirectoryName(paths[1]));
}

}  // namespace
}  // namespace component
