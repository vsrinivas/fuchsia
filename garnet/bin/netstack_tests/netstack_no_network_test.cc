// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/net/cpp/fidl.h>
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
class NetstackNoNetworkTest : public sys::testing::TestWithEnvironment {};

const char kNetstackUrl[] =
    "fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cmx";
const char kTopoPath[] = "/fake/topo/path";
const char kInterfaceName[] = "en0";
const char kTestNoNetworkClientUrl[] =
    "fuchsia-pkg://fuchsia.com/test_no_network_client#meta/"
    "test_no_network_client.cmx";

TEST_F(NetstackNoNetworkTest, DisableEthernetInterface) {
  auto services = CreateServices();

  fuchsia::sys::LaunchInfo netstack_launch_info;
  netstack_launch_info.url = kNetstackUrl;
  netstack_launch_info.out = sys::CloneFileDescriptor(STDOUT_FILENO);
  netstack_launch_info.err = sys::CloneFileDescriptor(STDERR_FILENO);
  services->AddServiceWithLaunchInfo(std::move(netstack_launch_info),
                                     fuchsia::netstack::Netstack::Name_);

  fuchsia::sys::LaunchInfo socket_provider_launch_info;
  socket_provider_launch_info.url = kNetstackUrl;
  socket_provider_launch_info.out = sys::CloneFileDescriptor(STDOUT_FILENO);
  socket_provider_launch_info.err = sys::CloneFileDescriptor(STDERR_FILENO);
  services->AddServiceWithLaunchInfo(std::move(socket_provider_launch_info),
                                     fuchsia::net::SocketProvider::Name_);

  auto env = CreateNewEnclosingEnvironment(
      "NetstackNoNetworkTest_DisableEthernetInterface", std::move(services));
  ASSERT_TRUE(WaitForEnclosingEnvToStart(env.get()));

  auto eth_config = netemul::EthertapConfig("DisableEthernetInterface");
  auto tap = netemul::EthertapClient::Create(eth_config);
  ASSERT_TRUE(tap) << "failed to create ethertap device";
  tap->SetLinkUp(true);

  netemul::EthernetClientFactory eth_factory;
  auto eth = eth_factory.RetrieveWithMAC(eth_config.mac);
  ASSERT_TRUE(eth) << "failed to retrieve ethernet client";

  fuchsia::netstack::NetstackPtr netstack;
  env->ConnectToService(netstack.NewRequest());

  fuchsia::netstack::InterfaceConfig config;
  config.name = kInterfaceName;
  config.ip_address_config.set_dhcp(false);

  uint32_t eth_id = 0;
  netstack->AddEthernetDevice(std::move(kTopoPath), std::move(config),
                              eth->device(),
                              [&eth_id](uint32_t id) { eth_id = id; });
  ASSERT_TRUE(RunLoopUntil([&eth_id] { return eth_id != 0; }));

  inet::IpAddress ip = inet::IpAddress(192, 168, 0, 2);
  fuchsia::net::IpAddress addr;
  fuchsia::net::IPv4Address ipv4;
  memcpy(ipv4.addr.data(), ip.as_bytes(), 4);
  addr.set_ipv4(ipv4);

  fuchsia::netstack::Status net_status =
      fuchsia::netstack::Status::UNKNOWN_ERROR;
  netstack->SetInterfaceAddress(
      eth_id, std::move(addr), 32,
      [&net_status](fuchsia::netstack::NetErr result) {
        net_status = result.status;
      });
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&net_status] { return net_status == fuchsia::netstack::Status::OK; },
      zx::sec(10)));

  // Run the client without enabling the interface.

  fuchsia::sys::LaunchInfo client_launch_info;
  client_launch_info.url = kTestNoNetworkClientUrl;
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
  ASSERT_EQ(exit_code, 0) << "Exit code was non-zero, got: " << exit_code;
  ASSERT_EQ(term_reason, fuchsia::sys::TerminationReason::EXITED)
      << "TerminationReason was not 'EXITED' as expected, got: "
      << sys::TerminationReasonToString(term_reason);
}

}  // namespace
