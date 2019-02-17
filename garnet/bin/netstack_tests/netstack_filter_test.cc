// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/protocol/ethernet.h>
#include <fbl/auto_call.h>
#include <fuchsia/hardware/ethernet/cpp/fidl.h>
#include <fuchsia/net/filter/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <garnet/lib/inet/ip_address.h>
#include <lib/fdio/util.h>
#include <lib/fdio/watcher.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/sys/cpp/file_descriptor.h>
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
#include "lib/sys/cpp/termination_reason.h"

namespace {
class NetstackFilterTest : public component::testing::TestWithEnvironment {};

const char kEthernetDir[] = "/dev/class/ethernet";
const char kTapctl[] = "/dev/misc/tapctl";
const uint8_t kTapMac[] = {0x12, 0x20, 0x30, 0x40, 0x50, 0x60};
const zx::duration kTimeout = zx::sec(5);

// TODO(cgibson): These first couple of functions are all boilerplate code
// copied from https://fuchsia-review.googlesource.com/c/garnet/+/221747 --
// Work is in progress to turn this code into a separate library, at which point
// this can be refactored.
zx_status_t CreateEthertap(zx::socket* sock) {
  int ctlfd = open(kTapctl, O_RDONLY);
  if (ctlfd < 0) {
    fprintf(stderr, "could not open %s: %s\n", kTapctl, strerror(errno));
    return ZX_ERR_IO;
  }
  auto closer = fbl::MakeAutoCall([ctlfd]() { close(ctlfd); });

  ethertap_ioctl_config_t config = {};
  strlcpy(config.name, "netstack_filter_test", ETHERTAP_MAX_NAME_LEN);
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

fuchsia::sys::LaunchInfo CreateLaunchInfo(
    const std::string& url, const std::vector<std::string>& args = {}) {
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = url;
  for (const auto& a : args) {
    launch_info.arguments.push_back(a);
  }
  launch_info.out = sys::CloneFileDescriptor(1);
  launch_info.err = sys::CloneFileDescriptor(2);
  return launch_info;
}

fuchsia::sys::ComponentControllerPtr RunComponent(
    component::testing::EnclosingEnvironment* enclosing_environment,
    const std::string& url, const std::vector<std::string>& args = {}) {
  return enclosing_environment->CreateComponent(
      CreateLaunchInfo(url, std::move(args)));
}

bool WaitForNewInterface(
    const inet::IpAddress& test_static_ip,
    std::vector<fuchsia::netstack::NetInterface> interfaces) {
  inet::IpAddress ip_address;
  for (const auto& interface : interfaces) {
    ip_address = inet::IpAddress(&interface.addr);
    if (test_static_ip == ip_address) {
      return true;
    }
  }
  return false;
}

TEST_F(NetstackFilterTest, DISABLED_TestRuleset) {
  auto services = CreateServices();
  const std::string netstack_url =
      "fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cmx";
  fuchsia::sys::LaunchInfo netstack_launch_info =
      CreateLaunchInfo(netstack_url);
  zx_status_t status = services->AddServiceWithLaunchInfo(
      std::move(netstack_launch_info), fuchsia::netstack::Netstack::Name_);
  fprintf(stderr, "added netstack service!\n");

  fuchsia::sys::LaunchInfo filter_launch_info = CreateLaunchInfo(netstack_url);
  status = services->AddServiceWithLaunchInfo(
      std::move(filter_launch_info), fuchsia::net::filter::Filter::Name_);
  ASSERT_TRUE(status == ZX_OK);
  fprintf(stderr, "added filter service!\n");

  auto env = CreateNewEnclosingEnvironment("NetstackFilterTest_TestRules",
                                           std::move(services));
  ASSERT_TRUE(WaitForEnclosingEnvToStart(env.get()));

  zx::socket sock;
  status = CreateEthertap(&sock);
  ASSERT_EQ(ZX_OK, status);
  fprintf(stderr, "created tap device\n");

  zx::channel svc;
  status = OpenEthertapDev(&svc);
  ASSERT_EQ(ZX_OK, status);
  fprintf(stderr, "found tap device\n");

  status = sock.signal_peer(0u, ETHERTAP_SIGNAL_ONLINE);
  ASSERT_EQ(ZX_OK, status);
  fprintf(stderr, "set ethertap link status online\n");

  fuchsia::netstack::NetstackPtr netstack;
  env->ConnectToService(netstack.NewRequest());
  fidl::StringPtr topo_path = "/fake/device";

  fidl::StringPtr interface_name = "test_filter_interface";
  fuchsia::netstack::InterfaceConfig config =
      fuchsia::netstack::InterfaceConfig{};
  config.name = interface_name;

  inet::IpAddress test_static_ip =
      inet::IpAddress::FromString("192.168.250.1", AF_INET);
  ASSERT_NE(test_static_ip, inet::IpAddress::kInvalid)
      << "Failed to create static IP address: "
      << test_static_ip.ToString().c_str();
  fprintf(stderr, "created static ip address: %s\n",
          test_static_ip.ToString().c_str());

  fuchsia::net::Subnet subnet;
  fuchsia::net::IPv4Address ipv4;
  memcpy(ipv4.addr.data(), test_static_ip.as_bytes(), 4);
  subnet.addr.set_ipv4(ipv4);
  subnet.prefix_len = 24;
  config.ip_address_config.set_static_ip(std::move(subnet));
  netstack->AddEthernetDevice(
      std::move(topo_path), std::move(config),
      fidl::InterfaceHandle<::fuchsia::hardware::ethernet::Device>(
          std::move(svc)),
      [&](uint32_t id) {});
  fprintf(stderr, "added new ethernet device\n");

  // Fetch the interface list synchronously so that we can make sure that our
  // interface is added correctly before continuing.
  static inet::IpAddress ip_address;
  bool found_static_ip_on_interface = false;
  netstack.events().OnInterfacesChanged =
      [&test_static_ip, &found_static_ip_on_interface](
          std::vector<fuchsia::netstack::NetInterface> interfaces) {
        found_static_ip_on_interface =
            WaitForNewInterface(test_static_ip, std::move(interfaces));
      };
  ASSERT_TRUE(RealLoopFixture::RunLoopWithTimeoutOrUntil(
      [&found_static_ip_on_interface] { return found_static_ip_on_interface; },
      kTimeout))
      << "Timed out waiting for netstack interface to appear!";

  ASSERT_TRUE(found_static_ip_on_interface)
      << "Static IP address was not found in the interface list!";

  // Launch the test program.
  const std::string filter_client_url =
      "fuchsia-pkg://fuchsia.com/test_filter_client#meta/"
      "test_filter_client.cmx";
  std::vector<std::string> args = {test_static_ip.ToString()};
  auto controller = RunComponent(env.get(), filter_client_url, args);
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
  ASSERT_TRUE(exit_code == 0) << "Exit code was non-zero, got: " << exit_code;
  ASSERT_TRUE(term_reason == fuchsia::sys::TerminationReason::EXITED)
      << "TerminationReason was not 'EXITED' as expected, got: "
      << sys::TerminationReasonToString(term_reason);
}
}  // namespace
