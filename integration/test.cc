// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/integration/test.h"

#include "lib/mtl/tasks/message_loop.h"

constexpr auto kYieldBatchPeriod = ftl::TimeDelta::FromMilliseconds(1);

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
  // Based on those results, opt to remove thread sleep and use PostDelayedTask
  // with 1ms.

  // Combinations tried:
  //                      PostQuitTask QuitNow
  //               inline    no msgs    hang (invalid call per docs)
  // SetAfterTaskCallback     hang      hang
  //      PostDelayedTask      ok        ok
  mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
      [] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); },
      kYieldBatchPeriod);
  mtl::MessageLoop::GetCurrent()->Run();
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

void Sleep() {
  Sleep(1s);
}

MaxwellTestBase::MaxwellTestBase() {
  root_environment->CreateNestedEnvironment(
      test_environment_host_.PassBoundHandle(), GetProxy(&test_environment_),
      NULL);
  test_environment_host_.SetEnvironment(test_environment_.get());
  test_environment_->GetApplicationLauncher(GetProxy(&test_launcher_));
}

void MaxwellTestBase::StartAgent(
    const std::string& url,
    std::unique_ptr<maxwell::AgentEnvironmentHost> env_host) {
  fidl::InterfaceHandle<modular::ApplicationEnvironmentHost> env_host_handle =
      agent_host_bindings_.AddBinding(std::move(env_host));

  modular::ApplicationEnvironmentPtr agent_env;
  test_environment_->CreateNestedEnvironment(std::move(env_host_handle),
                                             GetProxy(&agent_env), NULL);

  modular::ApplicationLauncherPtr agent_launcher;
  agent_env->GetApplicationLauncher(GetProxy(&agent_launcher));

  auto launch_info = modular::ApplicationLaunchInfo::New();
  launch_info->url = url;
  agent_launcher->CreateApplication(std::move(launch_info), NULL);
}

modular::ServiceProviderPtr MaxwellTestBase::StartEngine(
    const std::string& url) {
  modular::ServiceProviderPtr services;
  auto launch_info = modular::ApplicationLaunchInfo::New();
  launch_info->url = url;
  launch_info->services = GetProxy(&services);
  test_launcher_->CreateApplication(std::move(launch_info), NULL);
  return services;
}

modular::ApplicationEnvironment* root_environment;

int main(int argc, char** argv) {
  mtl::MessageLoop loop;
  auto app_ctx = modular::ApplicationContext::CreateFromStartupInfo();
  root_environment = app_ctx->environment().get();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
