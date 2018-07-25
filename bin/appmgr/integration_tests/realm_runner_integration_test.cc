// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/integration_tests/mock_runner_registry.h"
#include "lib/component/cpp/testing/test_with_environment.h"

namespace component {
namespace {

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

  bool WaitForRunnerToDie() {
    const bool ret = RunLoopWithTimeoutOrUntil(
        [&] { return !runner_registry_.runner(); }, kTimeout);
    EXPECT_TRUE(ret) << "Waiting for connection timed out: "
                     << runner_registry_.dead_runner_count();
    return ret;
  }

  bool WaitForComponentStart(size_t expected_components_count) {
    auto runner = runner_registry_.runner();
    const bool ret = RunLoopWithTimeoutOrUntil(
        [&] {
          return runner->components().size() == expected_components_count;
        },
        kTimeout);
    EXPECT_TRUE(ret) << "Waiting for component to start timed out, got:"
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
  ASSERT_TRUE(WaitForComponentStart(1));
  auto components = runner_registry_.runner()->components();
  ASSERT_EQ(components[0].url, kComponentForRunnerUrl);
}

TEST_F(RealmRunnerTest, RunnerLaunchedOnlyOnce) {
  auto component =
      enclosing_environment_->CreateComponentFromUrl(kComponentForRunner);
  ASSERT_TRUE(WaitForRunnerToRegister());
  // launch again and check that runner was not executed again
  component =
      enclosing_environment_->CreateComponentFromUrl(kComponentForRunner);

  WaitForComponentStart(2);
  EXPECT_EQ(1, runner_registry_.connect_count());
}

TEST_F(RealmRunnerTest, RunnerLaunchedAgainWhenKilled) {
  auto component =
      enclosing_environment_->CreateComponentFromUrl(kComponentForRunner);
  ASSERT_TRUE(WaitForRunnerToRegister());
  runner_registry_.runner()->runner_ptr()->Kill();
  ASSERT_TRUE(WaitForRunnerToDie());

  // launch again and check that runner was executed again
  component =
      enclosing_environment_->CreateComponentFromUrl(kComponentForRunner);
  ASSERT_TRUE(WaitForRunnerToRegister());
  ASSERT_EQ(2, runner_registry_.connect_count());
  // make sure component was also launched
  ASSERT_TRUE(WaitForComponentStart(1));
  auto components = runner_registry_.runner()->components();
  ASSERT_EQ(components[0].url, kComponentForRunnerUrl);
}

}  // namespace
}  // namespace component
