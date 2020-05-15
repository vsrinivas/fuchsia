// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/modular/testing/cpp/fake_component.h>
#include <lib/sys/cpp/service_directory.h>

#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"

class FakeComponentTest : public modular_testing::TestHarnessFixture {};

class TestComponent : public modular_testing::FakeComponent {
 public:
  // |on_created| is called when the component is launched. |on_destroyed| is called when the
  // component is terminated.
  TestComponent(fit::function<void()> on_created, fit::function<void()> on_destroyed)
      : FakeComponent({.url = modular_testing::TestHarnessBuilder::GenerateFakeUrl(),
                       .sandbox_services = {"fuchsia.modular.SessionShellContext"}}),
        on_created_(std::move(on_created)),
        on_destroyed_(std::move(on_destroyed)) {}

 protected:
  void OnCreate(fuchsia::sys::StartupInfo startup_info) override { on_created_(); }

  void OnDestroy() override { on_destroyed_(); }

  fit::function<void()> on_created_;
  fit::function<void()> on_destroyed_;
};

// Test that the options returned from FakeComponent::BuildInterceptOptions() intercepts and handles
// the component launch.
TEST_F(FakeComponentTest, BuildInterceptOptions) {
  modular_testing::FakeComponent fake_agent(
      {.url = modular_testing::TestHarnessBuilder::GenerateFakeUrl()});

  fuchsia::modular::testing::TestHarnessSpec spec;
  spec.mutable_sessionmgr_config()->mutable_session_agents()->push_back(fake_agent.url());

  modular_testing::TestHarnessBuilder builder(std::move(spec));
  builder.InterceptComponent(fake_agent.BuildInterceptOptions());
  builder.BuildAndRun(test_harness());

  RunLoopUntil([&] { return fake_agent.is_running(); });
}

// Tests overriding OnCreate/OnDestroy(), and the state change  is_running().
TEST_F(FakeComponentTest, OnCreateOnDestroy) {
  modular_testing::TestHarnessBuilder builder;

  bool running = false;
  TestComponent session_shell([&] { running = true; }, [&] { running = false; });
  builder.InterceptSessionShell(session_shell.BuildInterceptOptions());
  builder.BuildAndRun(test_harness());

  EXPECT_FALSE(session_shell.is_running());
  RunLoopUntil([&] { return session_shell.is_running(); });
  EXPECT_TRUE(running);

  fuchsia::modular::SessionShellContextPtr session_shell_context;
  session_shell.component_context()->svc()->Connect(session_shell_context.NewRequest());
  session_shell_context->Logout();

  RunLoopUntil([&] { return !session_shell.is_running(); });
  EXPECT_FALSE(running);
}

TEST_F(FakeComponentTest, Exit) {
  modular_testing::TestHarnessBuilder builder;
  modular_testing::FakeComponent session_shell(
      {.url = modular_testing::TestHarnessBuilder::GenerateFakeUrl()});
  builder.InterceptSessionShell(session_shell.BuildInterceptOptions());
  builder.BuildAndRun(test_harness());

  RunLoopUntil([&] { return session_shell.is_running(); });
  session_shell.Exit(0);
  RunLoopUntil([&] { return !session_shell.is_running(); });
}

// Tests that FakeComponent publishes & implements fuchsia.modular.Lifecycle.
TEST_F(FakeComponentTest, Lifecyle) {
  modular_testing::TestHarnessBuilder builder;
  modular_testing::FakeComponent session_shell(
      {.url = modular_testing::TestHarnessBuilder::GenerateFakeUrl()});
  builder.InterceptSessionShell(session_shell.BuildInterceptOptions());
  builder.BuildAndRun(test_harness());

  RunLoopUntil([&] { return session_shell.is_running(); });

  // Serve the outgoing() directory from FakeComponent.
  zx::channel svc_request, svc_dir;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &svc_request, &svc_dir));
  session_shell.component_context()->outgoing()->Serve(std::move(svc_request));
  sys::ServiceDirectory svc(std::move(svc_dir));

  fuchsia::modular::LifecyclePtr lifecycle;
  ASSERT_EQ(ZX_OK, svc.Connect(lifecycle.NewRequest(), "svc/fuchsia.modular.Lifecycle"));
  lifecycle->Terminate();
  RunLoopUntil([&] { return !session_shell.is_running(); });
}
