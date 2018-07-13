// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/component_controller_impl.h"
#include "garnet/bin/appmgr/realm.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/default.h>

#include "gtest/gtest.h"
#include "lib/app/cpp/environment_services.h"
#include "lib/gtest/real_loop_fixture.h"

namespace component {
namespace {

class RealmTest : public gtest::RealLoopFixture {};

fuchsia::sys::ComponentControllerPtr RunComponent(
    fuchsia::sys::LauncherSync2Ptr& launcher, std::string component_url,
    int64_t expected_return_code) {
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = component_url;

  fuchsia::sys::ComponentControllerPtr controller;
  launcher->CreateComponent(std::move(launch_info), controller.NewRequest());

  return controller;
}

// This test exercises the fact that two components should be in separate jobs,
// and thus when one component controller kills its job due to a .Kill() call
// the other component should run uninterrupted.
TEST_F(RealmTest, CreateTwoKillOne) {
  // Connect to the Launcher service through our static environment.
  // This launcher is from sys realm so our hub would be scoped to it
  fuchsia::sys::LauncherSync2Ptr launcher;
  fuchsia::sys::ConnectToEnvironmentService(launcher.NewRequest());

  // Launch two components
  fuchsia::sys::ComponentControllerPtr controller1 =
      RunComponent(launcher, "/boot/bin/sh", 0);

  fuchsia::sys::ComponentControllerPtr controller2 =
      RunComponent(launcher, "/boot/bin/sh", 0);

  bool controller2_had_error = false;
  controller2.set_error_handler(
      [&controller2_had_error] { controller2_had_error = true; });

  // Kill one of the two components, make sure it's exited via Wait

  bool wait = false;
  controller1->Wait([&wait](int64_t errcode) { wait = true; });
  controller1->Kill();
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([&wait] { return wait; }, zx::sec(5)));

  // Make sure the second controller didn't have any errors
  EXPECT_FALSE(RunLoopWithTimeoutOrUntil(
      [&controller2_had_error] { return controller2_had_error; }, zx::sec(2)));

  // Kill the other component
  controller2->Kill();

  RunLoopUntilIdle();
}

}  // namespace
}  // namespace component
