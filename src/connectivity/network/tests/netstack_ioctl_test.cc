// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/netstack/cpp/fidl.h>
#include <lib/netstack/c/netconfig.h>
#include <lib/sys/cpp/file_descriptor.h>
#include <lib/sys/cpp/termination_reason.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <src/connectivity/network/testing/netemul/lib/network/ethernet_client.h>
#include <src/connectivity/network/testing/netemul/lib/network/ethertap_client.h>

#include "gtest/gtest.h"

namespace {
class NetstackIoctlTest : public sys::testing::TestWithEnvironment {};

const char kNetstackUrl[] =
    "fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cmx";
const char kTestIoctlClientUrl[] =
    "fuchsia-pkg://fuchsia.com/test_ioctl_client#meta/test_ioctl_client.cmx";
const char kTopoPath[] = "/fake/topo/path";
const char kInterfaceName[] = "en0";

TEST_F(NetstackIoctlTest, RunIoctlClient) {
  auto services = CreateServices();

  fuchsia::sys::LaunchInfo netstack_launch_info;
  netstack_launch_info.url = kNetstackUrl;
  netstack_launch_info.out = sys::CloneFileDescriptor(STDOUT_FILENO);
  netstack_launch_info.err = sys::CloneFileDescriptor(STDERR_FILENO);
  services->AddServiceWithLaunchInfo(std::move(netstack_launch_info),
                                     fuchsia::netstack::Netstack::Name_);

  // There's not currently a way to register one component for multiple
  // services. Duplicate component URLs will only be launched once, so there
  // will only be one netstack process running in this hermetic environment.
  fuchsia::sys::LaunchInfo socket_provider_launch_info;
  socket_provider_launch_info.url = kNetstackUrl;
  socket_provider_launch_info.out = sys::CloneFileDescriptor(STDOUT_FILENO);
  socket_provider_launch_info.err = sys::CloneFileDescriptor(STDERR_FILENO);
  services->AddServiceWithLaunchInfo(std::move(socket_provider_launch_info),
                                     fuchsia::net::SocketProvider::Name_);

  auto env = CreateNewEnclosingEnvironment("NetstackLaunchTest_IoctlTest",
                                           std::move(services));
  ASSERT_TRUE(WaitForEnclosingEnvToStart(env.get()));

  // Create a tap device so both the loopback and non-loopback code paths in
  // ioctl_netc_get_ifinfo are exercised.
  auto eth_config = netemul::EthertapConfig("IoctlTest");
  auto tap = netemul::EthertapClient::Create(eth_config);
  ASSERT_TRUE(tap) << "failed to create ethertap device";
  tap->SetLinkUp(true);

  netemul::EthernetClientFactory eth_factory;
  auto eth = eth_factory.RetrieveWithMAC(eth_config.tap_cfg.mac);
  ASSERT_TRUE(eth) << "failed to retrieve ethernet client";

  fuchsia::netstack::NetstackPtr netstack;
  env->ConnectToService(netstack.NewRequest());

  fuchsia::netstack::InterfaceConfig config;
  config.name = kInterfaceName;
  config.ip_address_config.set_dhcp(true);

  uint32_t eth_id = 0;
  netstack->AddEthernetDevice(std::move(kTopoPath), std::move(config),
                              std::move(eth->device()),
                              [&eth_id](uint32_t id) { eth_id = id; });
  ASSERT_TRUE(RunLoopUntil([&eth_id] { return eth_id != 0; }));

  fuchsia::sys::LaunchInfo client_launch_info;
  client_launch_info.url = kTestIoctlClientUrl;
  client_launch_info.out = sys::CloneFileDescriptor(STDOUT_FILENO);
  client_launch_info.err = sys::CloneFileDescriptor(STDERR_FILENO);
  auto controller = env.get()->CreateComponent(std::move(client_launch_info));

  bool wait = false;
  int64_t exit_code;
  fuchsia::sys::TerminationReason term_reason;
  controller.events().OnTerminated =
      [&wait, &exit_code, &term_reason](
          int64_t retcode, fuchsia::sys::TerminationReason reason) {
        wait = true;
        exit_code = retcode;
        term_reason = reason;
      };
  EXPECT_TRUE(RunLoopUntil([&wait] { return wait; }));
  EXPECT_EQ(exit_code, 0);
  EXPECT_EQ(term_reason, fuchsia::sys::TerminationReason::EXITED)
      << "got: " << sys::TerminationReasonToString(term_reason);
}

}  // namespace
