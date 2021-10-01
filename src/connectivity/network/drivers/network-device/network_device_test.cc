// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "network_device.h"

#include <ddktl/device.h>
#include <gtest/gtest.h>

#include "device/test_session.h"
#include "device/test_util.h"
#include "mac/test_util.h"
#include "src/devices/testing/mock-ddk/mock-device.h"
#include "src/lib/testing/predicates/status.h"

namespace {
// Enable timeouts only to test things locally, committed code should not use timeouts.
constexpr zx::duration kTestTimeout = zx::duration::infinite();
}  // namespace

namespace network {
namespace testing {

class NetDeviceDriverTest : public ::testing::Test {
 protected:
  // Use a nonzero port identifier to avoid default value traps.
  static constexpr uint8_t kPortId = 11;

  NetDeviceDriverTest() : loop_(&kAsyncLoopConfigNeverAttachToThread) {
    loop_.StartThread("net-device-driver-test");
  }

  void TearDown() override { UnbindAndRelease(); }

  void UnbindAndRelease() {
    if (MockDevice* dev = parent_->GetLatestChild(); dev != nullptr) {
      dev->UnbindOp();
      EXPECT_OK(dev->WaitUntilUnbindReplyCalled(zx::deadline_after(kTestTimeout)));
      dev->ReleaseOp();
    }
  }

  zx_status_t CreateDevice(bool with_mac = false) {
    auto proto = device_impl_.proto();
    parent_->AddProtocol(ZX_PROTOCOL_NETWORK_DEVICE_IMPL, proto.ops, proto.ctx);
    if (with_mac) {
      port_impl_.SetMac(mac_impl_.proto());
    }
    port_impl_.SetStatus(
        {.mtu = 2048, .flags = static_cast<uint32_t>(netdev::wire::StatusFlags::kOnline)});
    if (zx_status_t status = NetworkDevice::Create(nullptr, parent_.get()); status != ZX_OK) {
      return status;
    }
    port_impl_.AddPort(kPortId, device_impl_.client());
    return ZX_OK;
  }

  zx::status<fidl::WireSyncClient<netdev::Device>> ConnectNetDevice() {
    zx::status endpoints = fidl::CreateEndpoints<netdev::Device>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }
    zx::status client = [this]() -> zx::status<fidl::WireSyncClient<netdev::DeviceInstance>> {
      zx::status endpoints = fidl::CreateEndpoints<netdev::DeviceInstance>();
      if (endpoints.is_error()) {
        return endpoints.take_error();
      }
      auto [client_end, server_end] = std::move(*endpoints);
      fidl::BindServer(loop_.dispatcher(), std::move(server_end),
                       parent_->GetLatestChild()->GetDeviceContext<NetworkDevice>());
      return zx::ok(fidl::BindSyncClient(std::move(client_end)));
    }();
    if (client.is_error()) {
      return client.take_error();
    }
    auto [client_end, server_end] = std::move(*endpoints);
    fidl::WireResult result = client->GetDevice(std::move(server_end));
    if (zx_status_t status = result.status(); status != ZX_OK) {
      return zx::error(status);
    }

    return zx::ok(fidl::BindSyncClient(std::move(client_end)));
  }

  const FakeNetworkDeviceImpl& device_impl() const { return device_impl_; }
  FakeNetworkPortImpl& port_impl() { return port_impl_; }

 private:
  const std::shared_ptr<MockDevice> parent_ = MockDevice::FakeRootParent();
  async::Loop loop_;

  FakeMacDeviceImpl mac_impl_;
  FakeNetworkDeviceImpl device_impl_;
  FakeNetworkPortImpl port_impl_;
};

TEST_F(NetDeviceDriverTest, TestCreateSimple) { ASSERT_OK(CreateDevice()); }

TEST_F(NetDeviceDriverTest, TestOpenSession) {
  ASSERT_OK(CreateDevice());
  TestSession session;
  zx::status connect_result = ConnectNetDevice();
  ASSERT_OK(connect_result.status_value());
  fidl::WireSyncClient<netdev::Device>& netdevice = connect_result.value();
  ASSERT_OK(session.Open(netdevice, "test-session"));
  ASSERT_OK(AttachSessionPort(session, port_impl()));
  ASSERT_OK(
      device_impl().events().wait_one(kEventStart, zx::deadline_after(kTestTimeout), nullptr));
  UnbindAndRelease();
  ASSERT_OK(session.WaitClosed(zx::deadline_after(kTestTimeout)));
  // netdevice should also have been closed after device unbind:
  ASSERT_OK(netdevice.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::deadline_after(kTestTimeout),
                                         nullptr));
}

TEST_F(NetDeviceDriverTest, TestWatcherDestruction) {
  // Test that on device removal watcher channels get closed.
  ASSERT_OK(CreateDevice());

  zx::status connect_result = ConnectNetDevice();
  ASSERT_OK(connect_result.status_value());
  fidl::WireSyncClient<netdev::Device>& netdevice = connect_result.value();

  zx::status port_endpoints = fidl::CreateEndpoints<netdev::Port>();
  ASSERT_OK(port_endpoints.status_value());
  auto [port_client_end, port_server_end] = std::move(*port_endpoints);
  ASSERT_OK(netdevice.GetPort(kPortId, std::move(port_server_end)).status());
  auto port = fidl::BindSyncClient(std::move(port_client_end));

  zx::status endpoints = fidl::CreateEndpoints<netdev::StatusWatcher>();
  ASSERT_OK(endpoints.status_value());
  auto [client_end, server_end] = std::move(*endpoints);
  ASSERT_OK(port.GetStatusWatcher(std::move(server_end), 1).status());
  fidl::WireSyncClient watcher = fidl::BindSyncClient(std::move(client_end));
  ASSERT_OK(watcher.WatchStatus().status());
  UnbindAndRelease();
  // Watcher, port, and netdevice should all observe channel closure.
  ASSERT_OK(watcher.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::deadline_after(kTestTimeout),
                                       nullptr));
  ASSERT_OK(
      port.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::deadline_after(kTestTimeout), nullptr));
  ASSERT_OK(netdevice.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::deadline_after(kTestTimeout),
                                         nullptr));
}

}  // namespace testing
}  // namespace network
