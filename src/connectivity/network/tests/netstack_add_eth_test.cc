// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/ethernet/cpp/fidl.h>
#include <fuchsia/net/stack/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <lib/sys/cpp/file_descriptor.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <src/connectivity/network/testing/netemul/lib/network/ethernet_client.h>
#include <src/connectivity/network/testing/netemul/lib/network/ethertap_client.h>
#include <zircon/status.h>

#include "gtest/gtest.h"

namespace {
class NetstackLaunchTest : public sys::testing::TestWithEnvironment {};

const char kNetstackUrl[] =
    "fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cmx";

TEST_F(NetstackLaunchTest, AddEthernetInterface) {
  auto services = CreateServices();

  // TODO(NET-1818): parameterize this over multiple netstack implementations
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kNetstackUrl;
  launch_info.out = sys::CloneFileDescriptor(1);
  launch_info.err = sys::CloneFileDescriptor(2);
  services->AddServiceWithLaunchInfo(std::move(launch_info),
                                     fuchsia::net::stack::Stack::Name_);

  auto env = CreateNewEnclosingEnvironment("NetstackLaunchTest_AddEth",
                                           std::move(services));
  ASSERT_TRUE(WaitForEnclosingEnvToStart(env.get()));

  auto eth_config = netemul::EthertapConfig("AddEthernetInterface");
  auto tap = netemul::EthertapClient::Create(eth_config);
  ASSERT_TRUE(tap) << "failed to create ethertap device";

  netemul::EthernetClientFactory eth_factory;
  auto eth = eth_factory.RetrieveWithMAC(eth_config.mac);
  ASSERT_TRUE(eth) << "failed to retrieve ethernet client";

  bool list_ifs = false;
  fuchsia::net::stack::StackPtr stack;
  env->ConnectToService(stack.NewRequest());
  stack->ListInterfaces(
      [&](::std::vector<::fuchsia::net::stack::InterfaceInfo> interfaces) {
        for (const auto& iface : interfaces) {
          ASSERT_TRUE(iface.properties.features &
                      ::fuchsia::hardware::ethernet::INFO_FEATURE_LOOPBACK);
        }
        list_ifs = true;
      });
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&] { return list_ifs; }, zx::sec(5)));

  uint64_t eth_id = 0;
  fidl::StringPtr topo_path = "/fake/device";
  stack->AddEthernetInterface(
      std::move(topo_path), std::move(eth->device()),
      [&](std::unique_ptr<::fuchsia::net::stack::Error> err, uint64_t id) {
        if (err != nullptr) {
          fprintf(stderr, "error adding ethernet interface: %u\n",
                  static_cast<uint32_t>(err->type));
        } else {
          eth_id = id;
        }
      });
  ASSERT_TRUE(
      RunLoopWithTimeoutOrUntil([&] { return eth_id > 0; }, zx::sec(5)));

  list_ifs = false;
  stack->ListInterfaces(
      [&](::std::vector<::fuchsia::net::stack::InterfaceInfo> interfaces) {
        for (const auto& iface : interfaces) {
          if (iface.properties.features &
              ::fuchsia::hardware::ethernet::INFO_FEATURE_LOOPBACK) {
            continue;
          }
          ASSERT_EQ(eth_id, iface.id);
          // tap device is created with link down, so we expect physical status
          // to be DOWN.
          EXPECT_EQ(iface.properties.physicalStatus,
                    fuchsia::net::stack::PhysicalStatus::DOWN);
        }
        list_ifs = true;
      });
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&] { return list_ifs; }, zx::sec(5)));
}

TEST_F(NetstackLaunchTest, AddEthernetDevice) {
  auto services = CreateServices();

  // TODO(NET-1818): parameterize this over multiple netstack implementations
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kNetstackUrl;
  launch_info.out = sys::CloneFileDescriptor(1);
  launch_info.err = sys::CloneFileDescriptor(2);
  services->AddServiceWithLaunchInfo(std::move(launch_info),
                                     fuchsia::netstack::Netstack::Name_);

  auto env = CreateNewEnclosingEnvironment("NetstackLaunchTest_AddEth",
                                           std::move(services));
  ASSERT_TRUE(WaitForEnclosingEnvToStart(env.get()));

  auto eth_config = netemul::EthertapConfig("AddEthernetDevice");
  auto tap = netemul::EthertapClient::Create(eth_config);
  ASSERT_TRUE(tap) << "failed to create ethertap device";

  netemul::EthernetClientFactory eth_factory;
  auto eth = eth_factory.RetrieveWithMAC(eth_config.mac);
  ASSERT_TRUE(eth) << "failed to retrieve ethernet client";

  bool list_ifs = false;
  fuchsia::netstack::NetstackPtr netstack;
  env->ConnectToService(netstack.NewRequest());
  fidl::StringPtr topo_path = "/fake/device";
  fidl::StringPtr interface_name = "en0";
  fuchsia::netstack::InterfaceConfig config =
      fuchsia::netstack::InterfaceConfig{};
  config.name = interface_name;
  config.ip_address_config.set_dhcp(true);
  netstack->GetInterfaces(
      [&](::std::vector<::fuchsia::netstack::NetInterface> interfaces) {
        for (const auto& iface : interfaces) {
          ASSERT_TRUE(iface.features &
                      ::fuchsia::hardware::ethernet::INFO_FEATURE_LOOPBACK);
        }
        list_ifs = true;
      });
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&] { return list_ifs; }, zx::sec(5)));

  uint32_t eth_id = 0;
  netstack->AddEthernetDevice(std::move(topo_path), std::move(config),
                              std::move(eth->device()),
                              [&](uint32_t id) { eth_id = id; });
  ASSERT_TRUE(
      RunLoopWithTimeoutOrUntil([&] { return eth_id > 0; }, zx::sec(5)));

  list_ifs = false;
  netstack->GetInterfaces(
      [&](::std::vector<::fuchsia::netstack::NetInterface> interfaces) {
        for (const auto& iface : interfaces) {
          if (iface.features &
              ::fuchsia::hardware::ethernet::INFO_FEATURE_LOOPBACK) {
            continue;
          }
          ASSERT_EQ(eth_id, iface.id);
          // tap device is created with link down, so we expect physical status
          // to be DOWN.
          EXPECT_EQ(iface.flags & fuchsia::netstack::NetInterfaceFlagUp, 0u);
        }
        list_ifs = true;
      });
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&] { return list_ifs; }, zx::sec(5)));
}

TEST_F(NetstackLaunchTest, DHCPRequestSent) {
  auto services = CreateServices();

  // TODO(NET-1818): parameterize this over multiple netstack implementations
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kNetstackUrl;
  launch_info.out = sys::CloneFileDescriptor(1);
  launch_info.err = sys::CloneFileDescriptor(2);
  zx_status_t status = services->AddServiceWithLaunchInfo(
      std::move(launch_info), fuchsia::netstack::Netstack::Name_);
  ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);

  auto env = CreateNewEnclosingEnvironment("NetstackDHCPTest_RequestSent",
                                           std::move(services));
  ASSERT_TRUE(WaitForEnclosingEnvToStart(env.get()));

  auto eth_config = netemul::EthertapConfig("DHCPRequestSent");
  auto tap = netemul::EthertapClient::Create(eth_config);
  ASSERT_TRUE(tap) << "failed to create ethertap device";
  tap->SetLinkUp(true);

  netemul::EthernetClientFactory eth_factory;
  auto eth = eth_factory.RetrieveWithMAC(eth_config.mac);
  ASSERT_TRUE(eth) << "failed to retrieve ethernet client";

  fuchsia::netstack::NetstackPtr netstack;
  env->ConnectToService(netstack.NewRequest());
  fidl::StringPtr topo_path = "/fake/device";

  fidl::StringPtr interface_name = "dhcp_test_interface";
  fuchsia::netstack::InterfaceConfig config =
      fuchsia::netstack::InterfaceConfig{};
  config.name = interface_name;
  config.ip_address_config.set_dhcp(true);

  bool data_callback_run = false;
  auto f = [&data_callback_run](std::vector<uint8_t> data) {
    auto len = data.size();
    const std::byte* ethbuf = reinterpret_cast<const std::byte*>(&data[0]);
    size_t expected_len = 302;
    size_t parsed = 0;

    EXPECT_EQ(len, (size_t)expected_len)
        << "got " << len << " bytes of " << expected_len << " requested\n";

    const std::byte ethertype = ethbuf[12];
    EXPECT_EQ((int)ethertype, 0x08);

    // TODO(stijlist): add an ETH_FRAME_MIN_HDR_SIZE to ddk's ethernet.h
    size_t eth_frame_min_hdr_size = 14;
    const std::byte* ip = &ethbuf[eth_frame_min_hdr_size];
    parsed += eth_frame_min_hdr_size;
    const std::byte protocol_number = ip[9];
    EXPECT_EQ((int)protocol_number, 17);

    size_t ihl = (size_t)(ip[0] & (std::byte)0x0f);
    size_t ip_bytes = (ihl * 32u) / 8u;

    const std::byte* udp = &ip[ip_bytes];
    parsed += ip_bytes;

    uint16_t src_port = (uint16_t)udp[0] << 8 | (uint8_t)udp[1];
    uint16_t dst_port = (uint16_t)udp[2] << 8 | (uint8_t)udp[3];

    // DHCP requests from netstack should come from port 68 (DHCP client) to
    // port 67 (DHCP server).
    EXPECT_EQ(src_port, 68u);
    EXPECT_EQ(dst_port, 67u);

    const std::byte* dhcp = &udp[8];
    // Assert the DHCP op type is DHCP request.
    const std::byte dhcp_op_type = dhcp[0];
    EXPECT_EQ((int)dhcp_op_type, 0x01);

    data_callback_run = true;
  };

  tap->SetPacketCallback(f);

  uint32_t nicid = 0;
  // TODO(NET-1864): migrate to fuchsia.net.stack.AddEthernetInterface when we
  // migrate netcfg to use AddEthernetInterface.
  netstack->AddEthernetDevice(std::move(topo_path), std::move(config),
                              std::move(eth->device()),
                              [&nicid](uint32_t id) { nicid = id; });

  ASSERT_TRUE(
      RunLoopWithTimeoutOrUntil([&] { return nicid != 0; }, zx::sec(5)));

  netstack->SetInterfaceStatus(nicid, true);
  fuchsia::netstack::Status net_status =
      fuchsia::netstack::Status::UNKNOWN_ERROR;
  netstack->SetDhcpClientStatus(
      nicid, true, [&net_status](fuchsia::netstack::NetErr result) {
        net_status = result.status;
      });

  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&] { return net_status == fuchsia::netstack::Status::OK; }, zx::sec(5)));

  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&data_callback_run] { return data_callback_run; }, zx::sec(5)));
}
}  // namespace
