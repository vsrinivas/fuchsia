// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <fuchsia/net/stack/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <lib/fdio/watcher.h>
#include <lib/zx/socket.h>
#include <zircon/device/ethertap.h>
#include <zircon/status.h>
#include <zircon/types.h>
#include "gtest/gtest.h"
#include "lib/component/cpp/testing/test_util.h"
#include "lib/component/cpp/testing/test_with_environment.h"

using namespace fuchsia::netstack;

namespace {

class NetstackLaunchTest : public component::testing::TestWithEnvironment {};

fuchsia::sys::LaunchInfo CreateLaunchInfo(const std::string& url) {
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = url;
  launch_info.out = component::testing::CloneFileDescriptor(1);
  launch_info.err = component::testing::CloneFileDescriptor(2);
  return launch_info;
}

// TODO(CP-144): enable when we can isolate /dev
TEST_F(NetstackLaunchTest, DISABLED_Launch) {
  auto services = CreateServices();
  auto netstack_launch = CreateLaunchInfo(
      "fuchsia-pkg://fuchsia.com/netstack_integration#meta/netstack.cmx");
  services->AddServiceWithLaunchInfo(std::move(netstack_launch),
                                     Netstack::Name_);
  auto env = CreateNewEnclosingEnvironment("NetstackLaunchTest_Launch",
                                           std::move(services));
  ASSERT_TRUE(WaitForEnclosingEnvToStart(env.get()));

  NetstackPtr netstack;
  env->ConnectToService(netstack.NewRequest());
  bool interfaces_gotten = false;
  netstack->GetInterfaces(
      [&](::fidl::VectorPtr<NetInterface>) { interfaces_gotten = true; });

  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&] { return interfaces_gotten; },
                                        zx::sec(10)));
}

}  // namespace
