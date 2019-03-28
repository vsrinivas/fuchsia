// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/net/filter/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <lib/netemul/network/ethernet_client.h>
#include <lib/netemul/network/ethertap_client.h>
#include <lib/netemul/network/ethertap_types.h>
#include <lib/sys/cpp/file_descriptor.h>
#include <lib/sys/cpp/termination_reason.h>
#include <lib/sys/cpp/testing/test_with_environment.h>

#include "garnet/lib/inet/ip_address.h"
#include "gtest/gtest.h"

namespace {
class NetstackFilterTest : public sys::testing::TestWithEnvironment {};

const zx::duration kTimeout = zx::sec(10);
const char kNetstackUrl[] =
    "fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cmx";
const char kTopoPath[] = "/fake/topo/path";
const char kInterfaceName[] = "en0";
const char kFilterClientUrl[] =
    "fuchsia-pkg://fuchsia.com/test_filter_client#meta/test_filter_client.cmx";

TEST_F(NetstackFilterTest, TestRuleset) {
  auto services = CreateServices();

  fuchsia::sys::LaunchInfo netstack_launch_info;
  netstack_launch_info.url = kNetstackUrl;
  netstack_launch_info.out = sys::CloneFileDescriptor(STDOUT_FILENO);
  netstack_launch_info.err = sys::CloneFileDescriptor(STDERR_FILENO);
  services->AddServiceWithLaunchInfo(std::move(netstack_launch_info),
                                     fuchsia::netstack::Netstack::Name_);

  fuchsia::sys::LaunchInfo filter_launch_info;
  filter_launch_info.url = kNetstackUrl;
  filter_launch_info.out = sys::CloneFileDescriptor(STDOUT_FILENO);
  filter_launch_info.err = sys::CloneFileDescriptor(STDERR_FILENO);
  services->AddServiceWithLaunchInfo(std::move(filter_launch_info),
                                     fuchsia::net::filter::Filter::Name_);

  auto env = CreateNewEnclosingEnvironment("NetstackFilterTest_TestRules",
                                           std::move(services));
  ASSERT_TRUE(WaitForEnclosingEnvToStart(env.get()));

  auto eth_config = netemul::EthertapConfig("TestRuleset");
  auto tap = netemul::EthertapClient::Create(eth_config);
  ASSERT_TRUE(tap) << "failed to create ethertap device";

  netemul::EthernetClientFactory eth_factory;
  auto eth = eth_factory.RetrieveWithMAC(eth_config.mac);
  ASSERT_TRUE(eth) << "failed to retrieve ethernet client";

  fuchsia::netstack::NetstackPtr netstack;
  env->ConnectToService(netstack.NewRequest());

  inet::IpAddress test_static_ip = inet::IpAddress(192, 168, 250, 1);
  ASSERT_NE(test_static_ip, inet::IpAddress::kInvalid)
      << "Failed to create static IP address: "
      << test_static_ip.ToString().c_str();
  fprintf(stderr, "created static ip address: %s\n",
          test_static_ip.ToString().c_str());

  fuchsia::net::Subnet subnet;
  fuchsia::net::Ipv4Address ipv4;
  memcpy(ipv4.addr.data(), test_static_ip.as_bytes(), 4);
  subnet.addr.set_ipv4(ipv4);
  subnet.prefix_len = 24;

  fuchsia::netstack::InterfaceConfig config;
  config.name = kInterfaceName;
  config.ip_address_config.set_static_ip(std::move(subnet));

  uint32_t eth_id = 0;
  netstack->AddEthernetDevice(std::move(kTopoPath), std::move(config),
                              std::move(eth->device()),
                              [&eth_id](uint32_t id) { eth_id = id; });
  ASSERT_TRUE(RunLoopUntil([&eth_id] { return eth_id != 0; }));

  // Launch the test program.
  std::vector<std::string> args = {test_static_ip.ToString()};

  fuchsia::sys::LaunchInfo client_launch_info;
  client_launch_info.url = kFilterClientUrl;
  client_launch_info.out = sys::CloneFileDescriptor(STDOUT_FILENO);
  client_launch_info.err = sys::CloneFileDescriptor(STDERR_FILENO);
  for (const auto& a : args) {
    client_launch_info.arguments.push_back(a);
  }
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
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([&wait] { return wait; }, kTimeout));
  EXPECT_EQ(exit_code, 0) << "Exit code was non-zero, got: " << exit_code;
  EXPECT_EQ(term_reason, fuchsia::sys::TerminationReason::EXITED)
      << "TerminationReason was not 'EXITED' as expected, got: "
      << sys::TerminationReasonToString(term_reason);
}
}  // namespace
