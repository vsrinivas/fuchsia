// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/tests/maxwell_integration/test.h"

#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/synchronization/sleep.h"

constexpr auto kYieldSleepPeriod = fxl::TimeDelta::FromMilliseconds(1);
constexpr auto kYieldBatchPeriod = fxl::TimeDelta::FromMilliseconds(0);

void Yield() {
  // Tried a combination of Thread::sleep_for (formerly required) and
  // PostDelayedTask delays for a particular test sequence:
  //
  //        PostDelayedTask
  // s        0ms  1ms
  // l   w/o: 9.8s 8s
  // e   1ns: 8s
  // e   1ms: 7.9s 7.9s
  // p  10ms: 8s
  //
  // However, we've observed some additional flakiness in the Launcher tests
  // without the sleep.
  //
  // Based on those results, opt to sleep 1ms; post delayed w/ 0ms.
  fxl::SleepFor(kYieldSleepPeriod);

  // Combinations tried:
  //                      PostQuitTask QuitNow
  //               inline    no msgs    hang (invalid call per docs)
  // SetAfterTaskCallback     hang      hang
  //      PostDelayedTask      ok        ok
  fsl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
      [] { fsl::MessageLoop::GetCurrent()->PostQuitTask(); },
      kYieldBatchPeriod);
  fsl::MessageLoop::GetCurrent()->Run();
}

Predicate operator&&(const Predicate& a, const Predicate& b) {
  return [&a, &b] { return a() && b(); };
}

Predicate operator||(const Predicate& a, const Predicate& b) {
  return [&a, &b] { return a() || b(); };
}

Predicate operator!(const Predicate& a) {
  return [&a] { return !a(); };
}

Predicate Deadline(const fxl::TimeDelta& duration) {
  const auto deadline = fxl::TimePoint::Now() + duration;
  return [deadline] { return fxl::TimePoint::Now() >= deadline; };
}

void Sleep(const fxl::TimeDelta& duration) {
  WaitUntil(Deadline(duration));
}

void Sleep() {
  Sleep(fxl::TimeDelta::FromMilliseconds(1500));
}

namespace maxwell {

// Docs for app::ApplicationContext::CreateFromStartupInfo() say we should only
// call it one time. Although things functioned fine calling it many times in a
// test, for correctness we just keep a global around and initialize it once in
// the MaxwellTestBase() constructor.
std::unique_ptr<app::ApplicationContext> startup_context_;

MaxwellTestBase::MaxwellTestBase() {
  if (!startup_context_) {
    startup_context_ = app::ApplicationContext::CreateFromStartupInfo();
  }
  auto root_environment = startup_context_->environment().get();
  FXL_CHECK(root_environment != nullptr);

  test_environment_host_.reset(
      new ApplicationEnvironmentHostImpl(root_environment));
  test_environment_host_binding_.reset(
      new fidl::Binding<app::ApplicationEnvironmentHost>(
          test_environment_host_.get()));

  fidl::InterfaceHandle<app::ApplicationEnvironmentHost>
      test_environment_host_handle;
  test_environment_host_binding_->Bind(&test_environment_host_handle);
  root_environment->CreateNestedEnvironment(
      std::move(test_environment_host_handle), test_environment_.NewRequest(),
      test_environment_controller_.NewRequest(), "maxwell-test");
  test_environment_->GetApplicationLauncher(test_launcher_.NewRequest());
  agent_launcher_ =
      std::make_unique<maxwell::AgentLauncher>(test_environment_.get());
}

app::ServiceProviderPtr MaxwellTestBase::StartServiceProvider(
    const std::string& url) {
  app::ServiceProviderPtr services;
  auto launch_info = app::ApplicationLaunchInfo::New();
  launch_info->url = url;
  launch_info->services = services.NewRequest();

  test_launcher_->CreateApplication(std::move(launch_info), nullptr);
  return services;
}

app::ApplicationEnvironment* MaxwellTestBase::root_environment() {
  return startup_context_->environment().get();
}

}  // namespace maxwell
