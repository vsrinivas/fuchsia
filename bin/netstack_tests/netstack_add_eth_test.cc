// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/protocol/ethernet.h>
#include <fbl/auto_call.h>
#include <fuchsia/hardware/ethernet/cpp/fidl.h>
#include <fuchsia/net/stack/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <lib/fdio/util.h>
#include <lib/fdio/watcher.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/zx/socket.h>
#include <zircon/device/ethertap.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <string>

#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "gtest/gtest.h"

#include "lib/component/cpp/testing/test_util.h"
#include "lib/component/cpp/testing/test_with_environment.h"

namespace {
class NetstackLaunchTest : public component::testing::TestWithEnvironment {};

const char kNetstackUrl[] = "fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cmx";
const char kEthernetDir[] = "/dev/class/ethernet";
const char kTapctl[] = "/dev/misc/tapctl";
const uint8_t kTapMac[] = {0x12, 0x20, 0x30, 0x40, 0x50, 0x60};

zx_status_t CreateEthertap(zx::socket* sock) {
  int ctlfd = open(kTapctl, O_RDONLY);
  if (ctlfd < 0) {
    fprintf(stderr, "could not open %s: %s\n", kTapctl, strerror(errno));
    return ZX_ERR_IO;
  }
  auto closer = fbl::MakeAutoCall([ctlfd]() { close(ctlfd); });

  ethertap_ioctl_config_t config = {};
  strlcpy(config.name, "netstack_add_eth_test", ETHERTAP_MAX_NAME_LEN);
  config.mtu = 1500;
  memcpy(config.mac, kTapMac, 6);
  ssize_t rc =
      ioctl_ethertap_config(ctlfd, &config, sock->reset_and_get_address());
  if (rc < 0) {
    zx_status_t status = static_cast<zx_status_t>(rc);
    fprintf(stderr, "could not configure ethertap device: %s\n",
            zx_status_get_string(status));
    return status;
  }
  return ZX_OK;
}

zx_status_t WatchCb(int dirfd, int event, const char* fn, void* cookie) {
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }
  if (!strcmp(fn, ".") || !strcmp(fn, "..")) {
    return ZX_OK;
  }

  zx::channel svc;
  {
    int devfd = openat(dirfd, fn, O_RDONLY);
    if (devfd < 0) {
      return ZX_OK;
    }

    zx_status_t status =
        fdio_get_service_handle(devfd, svc.reset_and_get_address());
    if (status != ZX_OK) {
      return status;
    }
  }

  ::fuchsia::hardware::ethernet::Device_SyncProxy dev(std::move(svc));
  // See if this device is our ethertap device
  fuchsia::hardware::ethernet::Info info;
  zx_status_t status = dev.GetInfo(&info);
  if (status != ZX_OK) {
    fprintf(stderr, "could not get ethernet info for %s/%s: %s\n", kEthernetDir,
            fn, zx_status_get_string(status));
    // Return ZX_OK to keep watching for devices.
    return ZX_OK;
  }
  if (!(info.features & fuchsia::hardware::ethernet::INFO_FEATURE_SYNTH)) {
    // Not a match, keep looking.
    return ZX_OK;
  }

  // Found it!
  // TODO(tkilbourn): this might not be the test device we created; need a
  // robust way of getting the name of the tap device to check. Note that
  // ioctl_device_get_device_name just returns "ethernet" since that's the child
  // of the tap device that we've opened here.
  auto svcp = reinterpret_cast<zx_handle_t*>(cookie);
  *svcp = dev.proxy().TakeChannel().release();
  return ZX_ERR_STOP;
}

zx_status_t OpenEthertapDev(zx::channel* svc) {
  if (svc == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  int ethdir = open(kEthernetDir, O_RDONLY);
  if (ethdir < 0) {
    fprintf(stderr, "could not open %s: %s\n", kEthernetDir, strerror(errno));
    return ZX_ERR_IO;
  }

  zx_status_t status;
  status = fdio_watch_directory(
      ethdir, WatchCb, zx_deadline_after(ZX_SEC(2)),
      reinterpret_cast<void*>(svc->reset_and_get_address()));
  if (status == ZX_ERR_STOP) {
    return ZX_OK;
  } else {
    return status;
  }
}

TEST_F(NetstackLaunchTest, AddEthernetInterface) {
  auto services = CreateServices();

  // TODO(NET-1818): parameterize this over multiple netstack implementations
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kNetstackUrl;
  launch_info.out = component::testing::CloneFileDescriptor(1);
  launch_info.err = component::testing::CloneFileDescriptor(2);
  services->AddServiceWithLaunchInfo(std::move(launch_info),
                                     fuchsia::net::stack::Stack::Name_);

  auto env = CreateNewEnclosingEnvironment("NetstackLaunchTest_AddEth",
                                           std::move(services));
  ASSERT_TRUE(WaitForEnclosingEnvToStart(env.get()));

  zx::socket sock;
  zx_status_t status = CreateEthertap(&sock);
  ASSERT_EQ(ZX_OK, status) << zx_status_get_string(status);
  zx::channel svc;
  status = OpenEthertapDev(&svc);
  ASSERT_EQ(ZX_OK, status) << zx_status_get_string(status);
  fprintf(stderr, "found tap device\n");

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
      std::move(topo_path),
      fidl::InterfaceHandle<::fuchsia::hardware::ethernet::Device>(
          std::move(svc)),
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
        }
        list_ifs = true;
      });
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&] { return list_ifs; }, zx::sec(5)));
}

TEST_F(NetstackLaunchTest, DISABLED_AddEthernetDevice) {
  auto services = CreateServices();

  // TODO(NET-1818): parameterize this over multiple netstack implementations
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kNetstackUrl;
  launch_info.out = component::testing::CloneFileDescriptor(1);
  launch_info.err = component::testing::CloneFileDescriptor(2);
  services->AddServiceWithLaunchInfo(std::move(launch_info),
                                     fuchsia::netstack::Netstack::Name_);

  auto env = CreateNewEnclosingEnvironment("NetstackLaunchTest_AddEth",
                                           std::move(services));
  ASSERT_TRUE(WaitForEnclosingEnvToStart(env.get()));

  zx::socket sock;
  zx_status_t status = CreateEthertap(&sock);
  ASSERT_EQ(ZX_OK, status) << zx_status_get_string(status);
  zx::channel svc;
  status = OpenEthertapDev(&svc);
  ASSERT_EQ(ZX_OK, status) << zx_status_get_string(status);
  fprintf(stderr, "found tap device\n");

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
  netstack->AddEthernetDevice(
      std::move(topo_path), std::move(config),
      fidl::InterfaceHandle<::fuchsia::hardware::ethernet::Device>(
          std::move(svc)),
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
        }
        list_ifs = true;
      });
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&] { return list_ifs; }, zx::sec(5)));
}

TEST_F(NetstackLaunchTest, DISABLED_DHCPRequestSent) {
  auto services = CreateServices();

  // TODO(NET-1818): parameterize this over multiple netstack implementations
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kNetstackUrl;
  launch_info.out = component::testing::CloneFileDescriptor(1);
  launch_info.err = component::testing::CloneFileDescriptor(2);
  zx_status_t status = services->AddServiceWithLaunchInfo(
      std::move(launch_info), fuchsia::netstack::Netstack::Name_);
  ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);

  auto env = CreateNewEnclosingEnvironment("NetstackDHCPTest_RequestSent",
                                           std::move(services));
  ASSERT_TRUE(WaitForEnclosingEnvToStart(env.get()));

  zx::socket sock;
  status = CreateEthertap(&sock);
  ASSERT_EQ(ZX_OK, status) << zx_status_get_string(status);
  zx::channel svc;
  status = OpenEthertapDev(&svc);
  ASSERT_EQ(ZX_OK, status) << zx_status_get_string(status);
  fprintf(stderr, "found tap device\n");

  status = sock.signal_peer(0u, ETHERTAP_SIGNAL_ONLINE);
  ASSERT_EQ(ZX_OK, status) << zx_status_get_string(status);
  fprintf(stderr, "set ethertap link status online\n");

  fuchsia::netstack::NetstackPtr netstack;
  env->ConnectToService(netstack.NewRequest());
  fidl::StringPtr topo_path = "/fake/device";

  fidl::StringPtr interface_name = "dhcp_test_interface";
  fuchsia::netstack::InterfaceConfig config =
      fuchsia::netstack::InterfaceConfig{};
  config.name = interface_name;
  config.ip_address_config.set_dhcp(true);

  // TODO(NET-1864): migrate to fuchsia.net.stack.AddEthernetInterface when we
  // migrate netcfg to use AddEthernetInterface.
  netstack->AddEthernetDevice(
      std::move(topo_path), std::move(config),
      fidl::InterfaceHandle<::fuchsia::hardware::ethernet::Device>(
          std::move(svc)),
      [&](uint32_t id) {});

  // Give zx_channel_write 10ms to enqueue whatever it needs, then run
  // until idle (reduces flake rate to zero).
  //
  // TODO(NET-1967): Figure out why this sleep is required. The call stack in
  // AddEthernetDevice is AddEthernetDevice -> ProxyController#Send ->
  // Message#Write -> zx_channel_write, none of which are asynchronous.
  zx::nanosleep(zx::deadline_after(zx::msec(10)));
  RealLoopFixture::RunLoopUntilIdle();

  std::byte buf[1500];
  size_t attempt_to_read = 1500;
  size_t read;
  size_t parsed = 0;
  zx_signals_t pending = 0;

  // Expected to take about ~150ms; we're being conservative to avoid flakes.
  status = sock.wait_one(ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED |
                             ZX_SOCKET_PEER_WRITE_DISABLED,
                         zx::clock::get_monotonic() + zx::msec(500), &pending);

  ASSERT_EQ(ZX_OK, status) << zx_status_get_string(status);
  ASSERT_GT((pending & ZX_SOCKET_READABLE), 0u)
      << "socket was not readable; signals: " << pending;
  status = sock.read(0, buf, attempt_to_read, &read);

  ASSERT_EQ(ZX_OK, status) << zx_status_get_string(status);
  ASSERT_GT(read, (size_t)0);
  EXPECT_EQ(read, (size_t)310)
      << "read " << read << " bytes of " << attempt_to_read << " requested\n";

  EXPECT_EQ((unsigned int)buf[0], ETHERTAP_MSG_PACKET)
      << "ethertap packet header incorrect";

  std::byte* eth = &buf[sizeof(ethertap_socket_header_t)];
  parsed += sizeof(ethertap_socket_header_t);
  std::byte ethertype = eth[12];
  EXPECT_EQ((int)ethertype, 0x08);

  // TODO(stijlist): add an ETH_FRAME_MIN_HDR_SIZE to ddk's ethernet.h
  size_t eth_frame_min_hdr_size = 14;
  std::byte* ip = &eth[eth_frame_min_hdr_size];
  parsed += eth_frame_min_hdr_size;
  std::byte protocol_number = ip[9];
  EXPECT_EQ((int)protocol_number, 17);

  size_t ihl = (size_t)(ip[0] & (std::byte)0x0f);
  size_t ip_bytes = (ihl * 32u) / 8u;

  std::byte* udp = &ip[ip_bytes];
  parsed += ip_bytes;

  uint16_t src_port = (uint16_t)udp[0] << 8 | (uint8_t)udp[1];
  uint16_t dst_port = (uint16_t)udp[2] << 8 | (uint8_t)udp[3];

  // DHCP requests from netstack should come from port 68 (DHCP client) to port
  // 67 (DHCP server).
  EXPECT_EQ(src_port, 68u);
  EXPECT_EQ(dst_port, 67u);

  std::byte* dhcp = &udp[8];
  // Assert the DHCP op type is DHCP request.
  std::byte dhcp_op_type = dhcp[0];
  EXPECT_EQ((int)dhcp_op_type, 0x01);
}
}  // namespace
