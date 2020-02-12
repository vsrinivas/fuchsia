// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "network_device.h"

#include <lib/fake_ddk/fake_ddk.h>

#include <ddktl/device.h>
#include <zxtest/cpp/zxtest.h>
#include <zxtest/zxtest.h>

#include "device/test_util.h"
#include "mac/test_util.h"

namespace {
// Enable timeouts only to test things locally, committed code should not use timeouts.
constexpr zx::duration kTestTimeout = zx::duration::infinite();
}  // namespace

namespace network {
namespace testing {

class NetDeviceDriverTest : public zxtest::Test, public fake_ddk::Bind {
 protected:
  typedef void(ReleaseOp)(void* ctx);

  void SetUp() override {
    // turn on trace logging to make debugging a bit easier
    __zircon_driver_rec__.log_flags = 0x00FF;
  }

  void TearDown() override {
    if (device_created_) {
      RemoveDeviceSync();
    }
  }

  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override {
    auto status = Bind::DeviceAdd(drv, parent, args, out);
    if (status == ZX_OK) {
      release_op_ = args->ops->release;
      device_created_ = true;
    }
    return status;
  }

  void UnbindDeviceSync() {
    if (!unbind_called_) {
      DeviceAsyncRemove(fake_ddk::kFakeDevice);
      EXPECT_OK(sync_completion_wait_deadline(&remove_called_sync_,
                                              zx::deadline_after(kTestTimeout).get()));
    }
  }

  void RemoveDeviceSync() {
    UnbindDeviceSync();
    if (release_op_) {
      release_op_(op_ctx_);
    }
    device_created_ = false;
  }

  zx_status_t CreateDevice(bool with_mac = false) {
    auto proto_count = with_mac ? 2 : 1;
    auto protos = new fake_ddk::ProtocolEntry[proto_count];
    auto device_proto = device_impl_.proto();
    protos[0] = {ZX_PROTOCOL_NETWORK_DEVICE_IMPL, {device_proto.ops, device_proto.ctx}};
    if (with_mac) {
      auto mac_proto = mac_impl_.proto();
      protos[1] = {ZX_PROTOCOL_MAC_ADDR_IMPL, {mac_proto.ops, mac_proto.ctx}};
    }

    SetProtocols(fbl::Array<fake_ddk::ProtocolEntry>(protos, proto_count));
    return NetworkDevice::Create(nullptr, fake_ddk::kFakeParent);
  }

  fit::result<netdev::Device::SyncClient, zx_status_t> ConnectNetDevice() {
    zx::channel client, req;
    zx_status_t status;
    if ((status = zx::channel::create(0, &client, &req)) != ZX_OK) {
      return fit::error(status);
    }

    auto result =
        netdev::DeviceInstance::Call::GetDevice(zx::unowned(FidlClient()), std::move(req));
    if (!result.ok()) {
      return fit::error(result.status());
    }

    return fit::ok(netdev::Device::SyncClient(std::move(client)));
  }

  fit::result<netdev::MacAddressing::SyncClient, zx_status_t> ConnectMac() {
    zx::channel client, req;
    zx_status_t status;
    if ((status = zx::channel::create(0, &client, &req)) != ZX_OK) {
      return fit::error(status);
    }

    auto result =
        netdev::DeviceInstance::Call::GetMacAddressing(zx::unowned(FidlClient()), std::move(req));
    if (!result.ok()) {
      return fit::error(result.status());
    }

    return fit::ok(netdev::MacAddressing::SyncClient(std::move(client)));
  }

  bool device_created_ = false;
  FakeMacDeviceImpl mac_impl_;
  FakeNetworkDeviceImpl device_impl_;
  ReleaseOp* release_op_;
};

TEST_F(NetDeviceDriverTest, TestCreateSimple) { ASSERT_OK(CreateDevice()); }

TEST_F(NetDeviceDriverTest, TestOpenSession) {
  ASSERT_OK(CreateDevice());
  TestSession session;
  auto connect_result = ConnectNetDevice();
  ASSERT_TRUE(connect_result.is_ok(), "Connect failed: %s",
              zx_status_get_string(connect_result.error()));
  auto netdevice = connect_result.take_value();
  ASSERT_OK(session.Open(zx::unowned(netdevice.channel()), "test-session"));
  session.SetPaused(false);
  ASSERT_OK(device_impl_.events().wait_one(kEventStart, zx::deadline_after(kTestTimeout), nullptr));
  UnbindDeviceSync();
  ASSERT_OK(session.WaitClosed(zx::deadline_after(kTestTimeout)));
  // netdevice should also have been closed after device unbind:
  ASSERT_OK(netdevice.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::deadline_after(kTestTimeout),
                                         nullptr));
}

TEST_F(NetDeviceDriverTest, TestWatcherDestruction) {
  // Test that on device removal watcher channels get closed.
  ASSERT_OK(CreateDevice());

  auto connect_result = ConnectNetDevice();
  ASSERT_TRUE(connect_result.is_ok(), "Connect failed: %s",
              zx_status_get_string(connect_result.error()));
  auto netdevice = connect_result.take_value();

  zx::channel req, watcher_channel;
  ASSERT_OK(zx::channel::create(0, &req, &watcher_channel));
  ASSERT_OK(netdevice.GetStatusWatcher(std::move(req), 1).status());
  netdev::StatusWatcher::SyncClient watcher(std::move(watcher_channel));
  ASSERT_OK(watcher.WatchStatus().status());
  UnbindDeviceSync();
  ASSERT_OK(watcher.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::deadline_after(kTestTimeout),
                                       nullptr));
  // netdevice should also have been closed after device unbind:
  ASSERT_OK(netdevice.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::deadline_after(kTestTimeout),
                                         nullptr));
}

TEST_F(NetDeviceDriverTest, TestMac) {
  ASSERT_OK(CreateDevice(true));

  auto connect_result = ConnectMac();
  ASSERT_TRUE(connect_result.is_ok(), "Connect failed: %s",
              zx_status_get_string(connect_result.error()));
  auto mac = connect_result.take_value();

  ASSERT_OK(mac.GetUnicastAddress().status());
  UnbindDeviceSync();
  // mac connection should be closed on device unbind:
  ASSERT_OK(
      mac.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::deadline_after(kTestTimeout), nullptr));
}

TEST_F(NetDeviceDriverTest, TestNoMac) {
  ASSERT_OK(CreateDevice(false));

  auto connect_result = ConnectMac();
  ASSERT_TRUE(connect_result.is_ok(), "Connect failed: %s",
              zx_status_get_string(connect_result.error()));
  auto mac = connect_result.take_value();

  ASSERT_EQ(mac.GetUnicastAddress().status(), ZX_ERR_PEER_CLOSED);
}

}  // namespace testing
}  // namespace network
