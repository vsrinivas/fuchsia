// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>
#include <fuchsia/net/stack/cpp/fidl.h>
#include <lib/fdio/util.h>
#include <lib/fdio/watcher.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/zx/socket.h>
#include <zircon/device/ethertap.h>
#include <zircon/ethernet/cpp/fidl.h>
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

  ::zircon::ethernet::Device_SyncProxy dev(std::move(svc));
  // See if this device is our ethertap device
  zircon::ethernet::Info info;
  zx_status_t status = dev.GetInfo(&info);
  if (status != ZX_OK) {
    fprintf(stderr, "could not get ethernet info for %s/%s: %s\n", kEthernetDir,
            fn, zx_status_get_string(status));
    // Return ZX_OK to keep watching for devices.
    return ZX_OK;
  }
  if (!(info.features & zircon::ethernet::INFO_FEATURE_SYNTH)) {
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

TEST_F(NetstackLaunchTest, AddEthernetDevice) {
  auto services = CreateServices();

  // TODO(NET-1818): parameterize this over multiple netstack implementations
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = "netstack";
  launch_info.out = component::testing::CloneFileDescriptor(1);
  launch_info.err = component::testing::CloneFileDescriptor(2);
  services->AddServiceWithLaunchInfo(std::move(launch_info),
                                     fuchsia::net::stack::Stack::Name_);

  auto env = CreateNewEnclosingEnvironment("NetstackLaunchTest_AddEth",
                                           std::move(services));
  ASSERT_TRUE(WaitForEnclosingEnvToStart(env.get()));

  zx::socket sock;
  zx_status_t status = CreateEthertap(&sock);
  EXPECT_EQ(ZX_OK, status);
  zx::channel svc;
  status = OpenEthertapDev(&svc);
  EXPECT_EQ(ZX_OK, status);
  fprintf(stderr, "found tap device\n");

  bool list_ifs = false;
  fuchsia::net::stack::StackPtr stack;
  env->ConnectToService(stack.NewRequest());
  stack->ListInterfaces(
      [&](::fidl::VectorPtr<::fuchsia::net::stack::InterfaceInfo> interfaces) {
        for (const auto& iface : *interfaces) {
          bool loopback = false;
          for (const auto& feat : *iface.features) {
            if (feat == ::fuchsia::net::stack::InterfaceFeature::loopback) {
              loopback = true;
              break;
            }
          }
          ASSERT_TRUE(loopback);
        }
        list_ifs = true;
      });
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&] { return list_ifs; }, zx::sec(5)));

  uint64_t eth_id = 0;
  fidl::StringPtr topo_path = "/fake/device";
  stack->AddEthernetInterface(
      std::move(topo_path),
      fidl::InterfaceHandle<::zircon::ethernet::Device>(std::move(svc)),
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
      [&](::fidl::VectorPtr<::fuchsia::net::stack::InterfaceInfo> interfaces) {
        for (const auto& iface : *interfaces) {
          bool loopback = false;
          for (const auto& feat : *iface.features) {
            if (feat == ::fuchsia::net::stack::InterfaceFeature::loopback) {
              loopback = true;
              break;
            }
          }
          if (loopback) {
            continue;
          }
          ASSERT_EQ(eth_id, iface.id);
        }
        list_ifs = true;
      });
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&] { return list_ifs; }, zx::sec(5)));
}
}  // namespace
