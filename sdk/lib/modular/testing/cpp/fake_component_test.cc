// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/async-testing/dispatcher_stub.h>
#include <lib/modular/testing/cpp/fake_component.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>

#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"

class FakeDispatcher : public async::DispatcherStub {
 public:
  zx_status_t BeginWait(async_wait_t* wait) override {
    last_wait = wait;
    return ZX_OK;
  }

  async_wait_t* last_wait = nullptr;
};

class FakeComponentTest : public modular_testing::TestHarnessFixture {
 protected:
  FakeDispatcher fake_dispatcher_;
  std::unique_ptr<modular_testing::FakeComponent> fake_component_;
};

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

// A FakeComponent that overrides OnCreateAsync.
//
// |on_created| takes a callback that serves the component's outgoing directory when invoked.
class TestComponentAsync : public modular_testing::FakeComponent {
 public:
  using OnCreateAsyncCallback = fit::function<void()>;

  // |on_created| is called when the component is launched. |on_destroyed| is called when the
  // component is terminated.
  TestComponentAsync(fit::function<void(OnCreateAsyncCallback)> on_created,
                     fit::function<void()> on_destroyed)
      : FakeComponent({.url = modular_testing::TestHarnessBuilder::GenerateFakeUrl(),
                       .sandbox_services = {"fuchsia.modular.SessionShellContext"}}),
        on_created_(std::move(on_created)),
        on_destroyed_(std::move(on_destroyed)) {}

 protected:
  void OnCreateAsync(fuchsia::sys::StartupInfo startup_info,
                     fit::function<void()> callback) override {
    on_created_(std::move(callback));
  }

  void OnCreate(fuchsia::sys::StartupInfo startup_info) override {
    // Not called.
  }

  void OnDestroy() override { on_destroyed_(); }

  fit::function<void(OnCreateAsyncCallback)> on_created_;
  fit::function<void()> on_destroyed_;
};

// Test that the options returned from FakeComponent::BuildInterceptOptions() intercepts and handles
// the component launch.
TEST_F(FakeComponentTest, BuildInterceptOptions) {
  fake_component_ =
      std::make_unique<modular_testing::FakeComponent>(modular_testing::FakeComponent::Args{
          .url = modular_testing::TestHarnessBuilder::GenerateFakeUrl()});

  fuchsia::modular::testing::TestHarnessSpec spec;
  spec.mutable_sessionmgr_config()->mutable_session_agents()->push_back(fake_component_->url());

  modular_testing::TestHarnessBuilder builder(std::move(spec));
  builder.InterceptComponent(fake_component_->BuildInterceptOptions());
  builder.BuildAndRun(test_harness());

  RunLoopUntil([&] { return fake_component_->is_running(); });
}

// Tests overriding OnCreate/OnDestroy(), and the state change is_running().
TEST_F(FakeComponentTest, OnCreateOnDestroy) {
  modular_testing::TestHarnessBuilder builder;

  bool running = false;
  fake_component_ =
      std::make_unique<TestComponent>([&] { running = true; }, [&] { running = false; });
  builder.InterceptSessionShell(fake_component_->BuildInterceptOptions());
  builder.BuildAndRun(test_harness());

  EXPECT_FALSE(fake_component_->is_running());
  RunLoopUntil([&] { return fake_component_->is_running(); });
  EXPECT_TRUE(running);

  fuchsia::modular::SessionShellContextPtr session_shell_context;
  fake_component_->component_context()->svc()->Connect(session_shell_context.NewRequest());
  session_shell_context->Logout();

  RunLoopUntil([&] { return !fake_component_->is_running(); });
  EXPECT_FALSE(running);
}

// Tests that overriding OnCreateAsync() and calling its callback causes the component's
// outgoing directory to be served.
TEST_F(FakeComponentTest, OnCreateAsyncServesOutgoing) {
  modular_testing::TestHarnessBuilder builder;

  fit::function<void()> serve_outgoing;
  bool running = false;
  fake_component_ = std::make_unique<TestComponentAsync>(
      [&](fit::function<void()> callback) {
        running = true;
        serve_outgoing = std::move(callback);
      },
      [&] { running = false; });

  // Wrap FakeComponent's launch_handler to store the handle for the request to the outgoing
  // directory so we can check when it's waited on.
  auto intercept_options = fake_component_->BuildInterceptOptions(&fake_dispatcher_);

  zx_handle_t directory_request_handle;
  auto new_launch_handler =
      [&, launch_handler = std::move(intercept_options.launch_handler)](
          fuchsia::sys::StartupInfo startup_info,
          fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent>
              intercepted_component) {
        directory_request_handle = startup_info.launch_info.directory_request.get();
        launch_handler(std::move(startup_info), std::move(intercepted_component));
      };

  intercept_options.launch_handler = std::move(new_launch_handler);

  builder.InterceptSessionShell(std::move(intercept_options));
  builder.BuildAndRun(test_harness());

  EXPECT_FALSE(fake_component_->is_running());
  RunLoopUntil([&] { return fake_component_->is_running(); });
  EXPECT_TRUE(running);
  EXPECT_TRUE(serve_outgoing);

  // The outgoing directory should not be served before the callback |serve_outgoing| is invoked.
  ASSERT_NE(nullptr, fake_dispatcher_.last_wait);
  EXPECT_NE(directory_request_handle, fake_dispatcher_.last_wait->object);

  serve_outgoing();

  EXPECT_EQ(directory_request_handle, fake_dispatcher_.last_wait->object);
}

TEST_F(FakeComponentTest, Exit) {
  modular_testing::TestHarnessBuilder builder;
  fake_component_ =
      std::make_unique<modular_testing::FakeComponent>(modular_testing::FakeComponent::Args{
          .url = modular_testing::TestHarnessBuilder::GenerateFakeUrl()});
  builder.InterceptSessionShell(fake_component_->BuildInterceptOptions());
  builder.BuildAndRun(test_harness());

  RunLoopUntil([&] { return fake_component_->is_running(); });
  fake_component_->Exit(0);
  RunLoopUntil([&] { return !fake_component_->is_running(); });
}

// Tests that FakeComponent publishes & implements fuchsia.modular.Lifecycle.
TEST_F(FakeComponentTest, Lifecyle) {
  modular_testing::TestHarnessBuilder builder;
  fake_component_ =
      std::make_unique<modular_testing::FakeComponent>(modular_testing::FakeComponent::Args{
          .url = modular_testing::TestHarnessBuilder::GenerateFakeUrl()});
  builder.InterceptSessionShell(fake_component_->BuildInterceptOptions());
  builder.BuildAndRun(test_harness());

  RunLoopUntil([&] { return fake_component_->is_running(); });

  // Serve the outgoing() directory from FakeComponent.
  zx::channel svc_request, svc_dir;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &svc_request, &svc_dir));
  fake_component_->component_context()->outgoing()->Serve(std::move(svc_request));
  sys::ServiceDirectory svc(std::move(svc_dir));

  fuchsia::modular::LifecyclePtr lifecycle;
  ASSERT_EQ(ZX_OK, svc.Connect(lifecycle.NewRequest(), "svc/fuchsia.modular.Lifecycle"));
  lifecycle->Terminate();
  RunLoopUntil([&] { return !fake_component_->is_running(); });
}
