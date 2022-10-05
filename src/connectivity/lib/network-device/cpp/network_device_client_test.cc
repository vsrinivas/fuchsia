// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/lib/network-device/cpp/network_device_client.h"

#include <fidl/fuchsia.net.tun/cpp/wire.h>
#include <lib/fpromise/bridge.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/time.h>
#include <zircon/status.h>

#include <memory>
#include <unordered_set>

#include <fbl/unique_fd.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/lib/testing/predicates/status.h"

namespace {

// Enable timeouts only to test things locally, committed code should not use timeouts.
constexpr zx::duration kTimeout = zx::duration::infinite();

namespace netdev = fuchsia_hardware_network;
namespace tun = fuchsia_net_tun;
using NetworkDeviceClient = network::client::NetworkDeviceClient;

template <class T>
class TestEventHandler : public fidl::WireAsyncEventHandler<T> {
 public:
  explicit TestEventHandler(const char* name) : name_(name) {}

  void on_fidl_error(fidl::UnbindInfo info) override {
    FAIL() << "Lost connection to " << name_ << ": " << info;
  }

 private:
  const char* name_;
};

class NetDeviceTest : public gtest::RealLoopFixture {
 public:
  // Use a non zero number to prevent default memory initialization from hiding bugs.
  static constexpr uint8_t kPortId = 4;
  NetDeviceTest() : gtest::RealLoopFixture() {}

  bool RunLoopUntilOrFailure(fit::function<bool()> f) {
    return RunLoopWithTimeoutOrUntil([f = std::move(f)] { return HasFailure() || f(); }, kTimeout,
                                     zx::duration::infinite());
  }

  tun::wire::DeviceConfig DefaultDeviceConfig() {
    tun::wire::DeviceConfig config(alloc_);
    config.set_blocking(true);
    return config;
  }

  tun::wire::DevicePairConfig DefaultPairConfig() {
    tun::wire::DevicePairConfig config(alloc_);
    return config;
  }

  tun::wire::BasePortConfig DefaultBasePortConfig() {
    tun::wire::BasePortConfig config(alloc_);
    config.set_id(kPortId);
    config.set_mtu(1500);
    const netdev::wire::FrameType rx_types[] = {
        netdev::wire::FrameType::kEthernet,
    };
    fidl::VectorView<netdev::wire::FrameType> rx_types_view(alloc_, std::size(rx_types));
    std::copy(std::begin(rx_types), std::end(rx_types), rx_types_view.data());
    const netdev::wire::FrameTypeSupport tx_types[] = {
        netdev::wire::FrameTypeSupport{
            .type = netdev::wire::FrameType::kEthernet,
            .features = netdev::wire::kFrameFeaturesRaw,
        },
    };
    fidl::VectorView<netdev::wire::FrameTypeSupport> tx_types_view(alloc_, std::size(tx_types));
    std::copy(std::begin(tx_types), std::end(tx_types), tx_types_view.data());
    config.set_rx_types(alloc_, rx_types_view);
    config.set_tx_types(alloc_, tx_types_view);
    return config;
  }

  tun::wire::DevicePairPortConfig DefaultPairPortConfig() {
    tun::wire::DevicePairPortConfig config(alloc_);
    config.set_base(alloc_, DefaultBasePortConfig());
    return config;
  }

  tun::wire::DevicePortConfig DefaultDevicePortConfig() {
    tun::wire::DevicePortConfig config(alloc_);
    config.set_base(alloc_, DefaultBasePortConfig());
    config.set_online(true);
    return config;
  }

  zx::status<fidl::ClientEnd<tun::Device>> OpenTunDevice(tun::wire::DeviceConfig config) {
    zx::status device_endpoints = fidl::CreateEndpoints<tun::Device>();
    if (device_endpoints.is_error()) {
      return device_endpoints.take_error();
    }
    zx::status tunctl = component::Connect<tun::Control>();
    if (tunctl.is_error()) {
      return tunctl.take_error();
    }
    fidl::WireSyncClient tun{std::move(tunctl.value())};
    if (zx_status_t status =
            tun->CreateDevice(std::move(config), std::move(device_endpoints->server)).status();
        status != ZX_OK) {
      return zx::error(status);
    }
    return zx::ok(std::move(device_endpoints->client));
  }

  zx::status<fidl::ClientEnd<tun::Port>> AddTunPort(const fidl::ClientEnd<tun::Device>& client,
                                                    tun::wire::DevicePortConfig config) {
    zx::status port_endpoints = fidl::CreateEndpoints<tun::Port>();
    if (port_endpoints.is_error()) {
      return port_endpoints.take_error();
    }
    fidl::WireResult result =
        fidl::WireCall(client)->AddPort(std::move(config), std::move(port_endpoints->server));
    if (result.status() != ZX_OK) {
      return zx::error(result.status());
    }
    return zx::ok(std::move(port_endpoints->client));
  }

  zx::status<std::tuple<fidl::WireSharedClient<tun::Device>, fidl::WireSharedClient<tun::Port>,
                        netdev::wire::PortId>>
  OpenTunDeviceAndPort(tun::wire::DeviceConfig device_config,
                       tun::wire::DevicePortConfig port_config) {
    zx::status device = OpenTunDevice(std::move(device_config));
    if (device.is_error()) {
      return device.take_error();
    }
    zx::status port = AddTunPort(*device, std::move(port_config));
    if (port.is_error()) {
      return port.take_error();
    }
    fidl::ClientEnd port_client = std::move(port.value());
    zx::status port_id = GetPortId([&port_client](fidl::ServerEnd<netdev::Port> server) {
      return fidl::WireCall(port_client)->GetPort(std::move(server)).status();
    });
    if (port_id.is_error()) {
      return port_id.take_error();
    }

    return zx::ok(std::make_tuple(
        fidl::WireSharedClient(std::move(*device), dispatcher(),
                               std::make_unique<TestEventHandler<tun::Device>>("tun device")),
        fidl::WireSharedClient(std::move(port_client), dispatcher(),
                               std::make_unique<TestEventHandler<tun::Port>>("tun port")),
        port_id.value()));
  }

  zx::status<std::tuple<fidl::WireSharedClient<tun::Device>, fidl::WireSharedClient<tun::Port>,
                        netdev::wire::PortId>>
  OpenTunDeviceAndPort() {
    return OpenTunDeviceAndPort(DefaultDeviceConfig(), DefaultDevicePortConfig());
  }

  static zx::status<fuchsia_hardware_network::wire::PortId> GetPortId(
      fit::function<zx_status_t(fidl::ServerEnd<fuchsia_hardware_network::Port>)> get_port) {
    zx::status endpoints = fidl::CreateEndpoints<fuchsia_hardware_network::Port>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }
    auto [client, server] = std::move(endpoints.value());
    if (zx_status_t status = get_port(std::move(server)); status != ZX_OK) {
      return zx::error(status);
    }

    fidl::WireResult result = fidl::WireCall(client)->GetInfo();
    if (!result.ok()) {
      return zx::error(result.status());
    }
    fuchsia_hardware_network::wire::PortInfo& port_info = result.value().info;
    if (!port_info.has_id()) {
      return zx::error(ZX_ERR_INTERNAL);
    }
    return zx::ok(port_info.id());
  }

  zx::status<fidl::WireSharedClient<tun::DevicePair>> OpenTunPair(
      tun::wire::DevicePairConfig config) {
    zx::status device_pair_endpoints = fidl::CreateEndpoints<tun::DevicePair>();
    if (device_pair_endpoints.is_error()) {
      return device_pair_endpoints.take_error();
    }
    zx::status tunctl = component::Connect<tun::Control>();
    if (tunctl.is_error()) {
      return tunctl.take_error();
    }
    fidl::WireSyncClient tun{std::move(tunctl.value())};
    if (zx_status_t status =
            tun->CreatePair(std::move(config), std::move(device_pair_endpoints->server)).status();
        status != ZX_OK) {
      return zx::error(status);
    }
    return zx::ok(fidl::WireSharedClient(
        std::move(device_pair_endpoints->client), dispatcher(),
        std::make_unique<TestEventHandler<tun::DevicePair>>("tun device pair")));
  }

  zx::status<fidl::WireSharedClient<tun::DevicePair>> OpenTunPair() {
    return OpenTunPair(DefaultPairConfig());
  }

  static void WaitTapOnlineInner(fidl::WireSharedClient<tun::Port>& tun_port,
                                 fit::callback<void()> complete) {
    tun_port->WatchState().ThenExactlyOnce(
        [&tun_port, complete = std::move(complete)](
            fidl::WireUnownedResult<tun::Port::WatchState>& result) mutable {
          if (!result.ok())
            return;
          fidl::WireResponse<tun::Port::WatchState>* response = result.Unwrap();
          if (response->state.has_session()) {
            complete();
          } else {
            WaitTapOnlineInner(tun_port, std::move(complete));
          }
        });
  }

  bool WaitTapOnline(fidl::WireSharedClient<tun::Port>& tun_port) {
    bool online = false;
    WaitTapOnlineInner(tun_port, [&online]() { online = true; });
    return RunLoopUntilOrFailure([&online]() { return online; });
  }

  fidl::ServerEnd<fuchsia_hardware_network::Device> CreateClientRequest(
      std::unique_ptr<NetworkDeviceClient>* out_client) {
    zx::status device_endpoints = fidl::CreateEndpoints<fuchsia_hardware_network::Device>();
    EXPECT_OK(device_endpoints.status_value());
    std::unique_ptr client =
        std::make_unique<NetworkDeviceClient>(std::move(device_endpoints->client));
    client->SetErrorCallback([](zx_status_t error) {
      FAIL() << "Client experienced error " << zx_status_get_string(error);
    });
    *out_client = std::move(client);
    return std::move(device_endpoints->server);
  }

  zx_status_t StartSession(NetworkDeviceClient& client) {
    std::optional<zx_status_t> opt;
    client.OpenSession("netdev_unittest", [&opt](zx_status_t status) { opt = status; });
    if (!RunLoopUntilOrFailure([&opt]() { return opt.has_value(); })) {
      return ZX_ERR_TIMED_OUT;
    }
    return opt.value();
  }

  zx_status_t AttachPort(NetworkDeviceClient& client, fuchsia_hardware_network::wire::PortId port,
                         std::vector<netdev::wire::FrameType> frame_types = {
                             netdev::wire::FrameType::kEthernet}) {
    std::optional<zx_status_t> opt;
    client.AttachPort(port, frame_types, [&opt](zx_status_t status) { opt = status; });
    if (!RunLoopUntilOrFailure([&opt] { return opt.has_value(); })) {
      return ZX_ERR_TIMED_OUT;
    }
    return opt.value();
  }

 protected:
  fidl::Arena<> alloc_;
};

TEST_F(NetDeviceTest, TestRxTx) {
  auto tun_device_result = OpenTunDeviceAndPort();
  ASSERT_OK(tun_device_result.status_value());
  auto& [tun_device, tun_port, port_id] = tun_device_result.value();
  std::unique_ptr<NetworkDeviceClient> client;
  ASSERT_OK(tun_device->GetDevice(CreateClientRequest(&client)).status());

  ASSERT_OK(StartSession(*client));
  ASSERT_OK(AttachPort(*client, port_id));
  ASSERT_TRUE(WaitTapOnline(tun_port));
  ASSERT_TRUE(client->HasSession());

  bool done = false;
  std::vector<uint8_t> send_data({0x01, 0x02, 0x03});
  client->SetRxCallback([&done, &send_data](NetworkDeviceClient::Buffer buffer) {
    done = true;
    auto& data = buffer.data();
    ASSERT_EQ(data.frame_type(), fuchsia_hardware_network::wire::FrameType::kEthernet);
    ASSERT_EQ(data.len(), send_data.size());
    ASSERT_EQ(data.parts(), 1u);
    ASSERT_EQ(memcmp(send_data.data(), data.part(0).data().data(), data.len()), 0);
  });

  bool wrote_frame = false;
  tun::wire::Frame frame(alloc_);
  frame.set_frame_type(netdev::wire::FrameType::kEthernet);
  frame.set_data(alloc_, fidl::VectorView<uint8_t>::FromExternal(send_data));
  frame.set_port(kPortId);
  tun_device->WriteFrame(frame).ThenExactlyOnce(
      [&wrote_frame](fidl::WireUnownedResult<tun::Device::WriteFrame>& call_result) {
        if (!call_result.ok()) {
          ADD_FAILURE() << "WriteFrame failed: " << call_result.error();
          return;
        }
        const fit::result<zx_status_t>* result = call_result.Unwrap();
        wrote_frame = true;
        if (result->is_error()) {
          FAIL() << "Failed to write to device " << zx_status_get_string(result->error_value());
        }
      });
  ASSERT_TRUE(RunLoopUntilOrFailure([&done, &wrote_frame]() { return done && wrote_frame; }))
      << "Timed out waiting for frame; done=" << done << ", wrote_frame=" << wrote_frame;

  done = false;
  tun_device->ReadFrame().ThenExactlyOnce(
      [&done, &send_data](fidl::WireUnownedResult<tun::Device::ReadFrame>& call_result) {
        if (!call_result.ok()) {
          ADD_FAILURE() << "ReadFrame failed: " << call_result.error();
          return;
        }
        done = true;
        const ::fit::result<zx_status_t, ::fuchsia_net_tun::wire::DeviceReadFrameResponse*>*
            result = call_result.Unwrap();
        if (result->is_error()) {
          FAIL() << "Failed to read from device " << zx_status_get_string(result->error_value());
        }
        ASSERT_EQ(result->value()->frame.frame_type(), netdev::wire::FrameType::kEthernet);
        const fidl::VectorView<uint8_t>& data = result->value()->frame.data();
        ASSERT_TRUE(
            std::equal(std::begin(data), std::end(data), send_data.begin(), send_data.end()));
      });

  auto tx = client->AllocTx();
  ASSERT_TRUE(tx.is_valid());
  tx.data().SetFrameType(fuchsia_hardware_network::wire::FrameType::kEthernet);
  tx.data().SetPortId(port_id);
  ASSERT_EQ(tx.data().Write(send_data.data(), send_data.size()), send_data.size());
  ASSERT_OK(tx.Send());

  ASSERT_TRUE(RunLoopUntilOrFailure([&done]() { return done; }));
}

TEST_F(NetDeviceTest, TestEcho) {
  auto tun_device_result = OpenTunDeviceAndPort();
  ASSERT_OK(tun_device_result.status_value());
  auto& [tun_device_bind, tun_port, port_id_bind] = tun_device_result.value();
  // Move into variable so we can capture in lambdas.
  fidl::WireSharedClient tun_device = std::move(tun_device_bind);
  const netdev::wire::PortId port_id = port_id_bind;
  std::unique_ptr<NetworkDeviceClient> client;
  ASSERT_OK(tun_device->GetDevice(CreateClientRequest(&client)).status());

  ASSERT_OK(StartSession(*client));
  ASSERT_OK(AttachPort(*client, port_id));
  ASSERT_TRUE(WaitTapOnline(tun_port));
  ASSERT_TRUE(client->HasSession());

  constexpr uint32_t kTestFrames = 128;
  fit::function<void()> write_frame;
  uint32_t frame_count = 0;
  fpromise::bridge<void, zx_status_t> write_bridge;
  write_frame = [this, &frame_count, &tun_device, &write_bridge, &write_frame]() {
    if (frame_count == kTestFrames) {
      write_bridge.completer.complete_ok();
    } else {
      tun::wire::Frame frame(alloc_);
      frame.set_frame_type(netdev::wire::FrameType::kEthernet);
      frame.set_data(alloc_, fidl::VectorView<uint8_t>::FromExternal(
                                 reinterpret_cast<uint8_t*>(&frame_count), sizeof(frame_count)));
      frame.set_port(kPortId);
      tun_device->WriteFrame(frame).ThenExactlyOnce(
          [&write_bridge,
           &write_frame](fidl::WireUnownedResult<tun::Device::WriteFrame>& call_result) {
            if (!call_result.ok()) {
              ADD_FAILURE() << "WriteFrame failed: " << call_result.error();
              return;
            }
            fit::result<zx_status_t>* result = call_result.Unwrap();
            if (result->is_error()) {
              write_bridge.completer.complete_error(result->error_value());
            }
            write_frame();
          });
      frame_count++;
    }
  };

  uint32_t echoed = 0;
  client->SetRxCallback([&client, &echoed, &port_id](NetworkDeviceClient::Buffer buffer) {
    echoed++;
    // Alternate between echoing with a copy and just descriptor swap.
    if (echoed % 2 == 0) {
      auto tx = client->AllocTx();
      ASSERT_TRUE(tx.is_valid()) << "Tx alloc failed at echo " << echoed;
      tx.data().SetFrameType(buffer.data().frame_type());
      tx.data().SetTxRequest(fuchsia_hardware_network::wire::TxFlags());
      tx.data().SetPortId(port_id);
      auto wr = tx.data().Write(buffer.data());
      EXPECT_EQ(wr, buffer.data().len());
      EXPECT_EQ(tx.Send(), ZX_OK);
    } else {
      buffer.data().SetTxRequest(fuchsia_hardware_network::wire::TxFlags());
      EXPECT_EQ(buffer.Send(), ZX_OK);
    }
  });
  write_frame();
  fpromise::result write_result = RunPromise(write_bridge.consumer.promise());
  ASSERT_TRUE(write_result.is_ok())
      << "WriteFrame error: " << zx_status_get_string(write_result.error());

  uint32_t waiting = 0;
  fit::function<void()> receive_frame;
  fpromise::bridge<void, zx_status_t> read_bridge;
  receive_frame = [&waiting, &tun_device, &read_bridge, &receive_frame]() {
    tun_device->ReadFrame().ThenExactlyOnce(
        [&read_bridge, &receive_frame,
         &waiting](fidl::WireUnownedResult<tun::Device::ReadFrame>& call_result) {
          if (!call_result.ok()) {
            ADD_FAILURE() << "ReadFrame failed: " << call_result.error();
            return;
          }
          const fit::result<zx_status_t, ::fuchsia_net_tun::wire::DeviceReadFrameResponse*>*
              result = call_result.Unwrap();
          if (result->is_error()) {
            read_bridge.completer.complete_error(result->error_value());
          }
          EXPECT_EQ(result->value()->frame.frame_type(), netdev::wire::FrameType::kEthernet);
          EXPECT_FALSE(result->value()->frame.has_meta());
          if (size_t count = result->value()->frame.data().count(); count != sizeof(uint32_t)) {
            ADD_FAILURE() << "Unexpected data size " << count;
          } else {
            uint32_t payload;
            memcpy(&payload, result->value()->frame.data().data(), sizeof(uint32_t));
            EXPECT_EQ(payload, waiting);
          }
          waiting++;
          if (waiting == kTestFrames) {
            read_bridge.completer.complete_ok();
          } else {
            receive_frame();
          }
        });
  };
  receive_frame();
  fpromise::result read_result = RunPromise(read_bridge.consumer.promise());
  ASSERT_TRUE(read_result.is_ok())
      << "ReadFrame failed " << zx_status_get_string(read_result.error());
  EXPECT_EQ(echoed, frame_count);
}

TEST_F(NetDeviceTest, TestEchoPair) {
  auto tun_pair_result = OpenTunPair();
  ASSERT_OK(tun_pair_result.status_value());
  auto& tun_pair = tun_pair_result.value();
  std::unique_ptr<NetworkDeviceClient> left, right;
  ASSERT_OK(tun_pair->GetLeft(CreateClientRequest(&left)).status());
  ASSERT_OK(tun_pair->GetRight(CreateClientRequest(&right)).status());
  {
    fidl::WireResult result = tun_pair.sync()->AddPort(DefaultPairPortConfig());
    ASSERT_OK(result.status());
    ASSERT_TRUE(result->is_ok()) << zx_status_get_string(result->error_value());
  }
  zx::status port_id = GetPortId([&tun_pair](fidl::ServerEnd<netdev::Port> port) {
    return tun_pair->GetLeftPort(kPortId, std::move(port)).status();
  });
  ASSERT_OK(port_id.status_value());
  const netdev::wire::PortId left_port_id = port_id.value();
  port_id = GetPortId([&tun_pair](fidl::ServerEnd<netdev::Port> port) {
    return tun_pair->GetRightPort(kPortId, std::move(port)).status();
  });
  ASSERT_OK(port_id.status_value());
  const netdev::wire::PortId right_port_id = port_id.value();

  ASSERT_OK(StartSession(*left));
  ASSERT_OK(StartSession(*right));
  ASSERT_OK(AttachPort(*left, left_port_id));
  ASSERT_OK(AttachPort(*right, right_port_id));

  left->SetRxCallback([&left, &left_port_id](NetworkDeviceClient::Buffer buffer) {
    auto tx = left->AllocTx();
    ASSERT_TRUE(tx.is_valid()) << "Tx alloc failed at echo";
    tx.data().SetFrameType(buffer.data().frame_type());
    tx.data().SetTxRequest(fuchsia_hardware_network::wire::TxFlags());
    tx.data().SetPortId(left_port_id);
    uint32_t pload;
    EXPECT_EQ(buffer.data().Read(&pload, sizeof(pload)), sizeof(pload));
    pload = ~pload;
    EXPECT_EQ(tx.data().Write(&pload, sizeof(pload)), sizeof(pload));
    EXPECT_EQ(tx.Send(), ZX_OK);
  });

  constexpr uint32_t kBufferCount = 128;
  uint32_t rx_counter = 0;

  fpromise::bridge completed_bridge;
  right->SetRxCallback([&rx_counter, &completed_bridge](NetworkDeviceClient::Buffer buffer) {
    uint32_t pload;
    EXPECT_EQ(buffer.data().frame_type(), fuchsia_hardware_network::wire::FrameType::kEthernet);
    EXPECT_EQ(buffer.data().len(), sizeof(pload));
    EXPECT_EQ(buffer.data().Read(&pload, sizeof(pload)), sizeof(pload));
    EXPECT_EQ(pload, ~rx_counter);
    rx_counter++;
    if (rx_counter == kBufferCount) {
      completed_bridge.completer.complete_ok();
    }
  });

  {
    fpromise::bridge online_bridge;
    auto status_handle = right->WatchStatus(
        right_port_id, [completer = std::move(online_bridge.completer)](
                           fuchsia_hardware_network::wire::PortStatus status) mutable {
          if (status.flags() & fuchsia_hardware_network::wire::StatusFlags::kOnline) {
            completer.complete_ok();
          }
        });
    ASSERT_TRUE(RunPromise(online_bridge.consumer.promise()).is_ok());
  }

  for (uint32_t i = 0; i < kBufferCount; i++) {
    auto tx = right->AllocTx();
    ASSERT_TRUE(tx.is_valid());
    tx.data().SetFrameType(fuchsia_hardware_network::wire::FrameType::kEthernet);
    tx.data().SetPortId(right_port_id);
    ASSERT_EQ(tx.data().Write(&i, sizeof(i)), sizeof(i));
    ASSERT_OK(tx.Send());
  }

  ASSERT_TRUE(RunPromise(completed_bridge.consumer.promise()).is_ok());
}

TEST_F(NetDeviceTest, StatusWatcher) {
  auto tun_device_result = OpenTunDeviceAndPort();
  ASSERT_OK(tun_device_result.status_value());
  auto& [tun_device, tun_port, port_id] = tun_device_result.value();
  std::unique_ptr<NetworkDeviceClient> client;
  ASSERT_OK(tun_device->GetDevice(CreateClientRequest(&client)).status());
  uint32_t call_count1 = 0;
  uint32_t call_count2 = 0;
  bool expect_online = true;

  auto watcher1 = client->WatchStatus(
      port_id, [&call_count1, &expect_online](fuchsia_hardware_network::wire::PortStatus status) {
        call_count1++;
        ASSERT_EQ(static_cast<bool>(status.flags() &
                                    fuchsia_hardware_network::wire::StatusFlags::kOnline),
                  expect_online)
            << "Unexpected status flags " << static_cast<uint32_t>(status.flags())
            << ", online should be " << expect_online;
      });

  {
    auto watcher2 = client->WatchStatus(
        port_id,
        [&call_count2](fuchsia_hardware_network::wire::PortStatus status) { call_count2++; });

    // Run loop with both watchers attached.
    ASSERT_TRUE(RunLoopUntilOrFailure([&call_count1, &call_count2]() {
      return call_count1 == 1 && call_count2 == 1;
    })) << "call_count1="
        << call_count1 << ", call_count2=" << call_count2;

    // Set online to false and wait for both watchers again.
    ASSERT_OK(tun_port.sync()->SetOnline(false).status());
    expect_online = false;
    ASSERT_TRUE(RunLoopUntilOrFailure([&call_count1, &call_count2]() {
      return call_count1 == 2 && call_count2 == 2;
    })) << "call_count1="
        << call_count1 << ", call_count2=" << call_count2;
  }
  // watcher2 goes out of scope, Toggle online 3 times and expect that call_count2 will
  // not increase.
  for (uint32_t i = 0; i < 3; i++) {
    expect_online = !expect_online;
    ASSERT_OK(tun_port.sync()->SetOnline(expect_online).status());
    ASSERT_TRUE(RunLoopUntilOrFailure([&call_count1, &i]() { return call_count1 == 3 + i; }))
        << "call_count1=" << call_count1 << ", call_count2=" << call_count2;
    // call_count2 mustn't change.
    ASSERT_EQ(call_count2, 2u);
  }
}

TEST_F(NetDeviceTest, ErrorCallback) {
  auto tun_device_result = OpenTunDeviceAndPort();
  ASSERT_OK(tun_device_result.status_value());
  auto& [tun_device, tun_port, port_id] = tun_device_result.value();
  std::unique_ptr<NetworkDeviceClient> client;
  ASSERT_OK(tun_device->GetDevice(CreateClientRequest(&client)).status());

  ASSERT_OK(StartSession(*client));
  ASSERT_OK(AttachPort(*client, port_id));
  ASSERT_TRUE(WaitTapOnline(tun_port));
  ASSERT_TRUE(client->HasSession());

  // Test error callback gets called when the session is killed.
  {
    std::optional<zx_status_t> opt;
    client->SetErrorCallback([&opt](zx_status_t status) { opt = status; });
    ASSERT_OK(client->KillSession());
    ASSERT_TRUE(RunLoopUntilOrFailure([&opt]() { return opt.has_value(); }));
    ASSERT_STATUS(opt.value(), ZX_ERR_CANCELED);
    ASSERT_FALSE(client->HasSession());
  }

  // Test error callback gets called when the device disappears.
  {
    std::optional<zx_status_t> opt;
    client->SetErrorCallback([&opt](zx_status_t status) { opt = status; });
    tun_device = {};
    ASSERT_TRUE(RunLoopUntilOrFailure([&opt]() { return opt.has_value(); }));
    ASSERT_STATUS(opt.value(), ZX_ERR_PEER_CLOSED);
  }
}

TEST_F(NetDeviceTest, PadTxFrames) {
  constexpr uint8_t kPayload[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A};
  constexpr uint32_t kMinBufferLength = static_cast<uint32_t>(sizeof(kPayload) - 2);
  constexpr size_t kSmallPayloadLength = kMinBufferLength - 2;
  auto device_config = DefaultDeviceConfig();
  tun::wire::BaseDeviceConfig base_config(alloc_);
  base_config.set_min_tx_buffer_length(kMinBufferLength);
  device_config.set_base(alloc_, std::move(base_config));

  auto tun_device_result =
      OpenTunDeviceAndPort(std::move(device_config), DefaultDevicePortConfig());
  ASSERT_OK(tun_device_result.status_value());
  auto& [tun_device, tun_port, port_id] = tun_device_result.value();

  std::unique_ptr<NetworkDeviceClient> client;
  ASSERT_OK(tun_device->GetDevice(CreateClientRequest(&client)).status());

  ASSERT_OK(StartSession(*client));
  ASSERT_OK(AttachPort(*client, port_id));
  ASSERT_TRUE(WaitTapOnline(tun_port));
  ASSERT_TRUE(client->HasSession());

  // Send three frames: one too small, one exactly minimum length, and one larger than minimum
  // length.
  for (auto& frame : {
           cpp20::span(kPayload, kSmallPayloadLength),
           cpp20::span(kPayload, kMinBufferLength),
           cpp20::span<const uint8_t>(kPayload),
       }) {
    auto tx = client->AllocTx();
    // Pollute buffer data first to check zero-padding.
    for (auto& b : tx.data().part(0).data()) {
      b = 0xAA;
    }
    ASSERT_TRUE(tx.is_valid());
    tx.data().SetFrameType(fuchsia_hardware_network::wire::FrameType::kEthernet);
    tx.data().SetPortId(port_id);
    EXPECT_EQ(tx.data().Write(frame.data(), frame.size()), frame.size());
    EXPECT_EQ(tx.Send(), ZX_OK);

    std::vector<uint8_t> expect(frame.begin(), frame.end());
    while (expect.size() < kMinBufferLength) {
      expect.push_back(0);
    }

    // Retrieve the frame and assert it's what we expect.
    bool done = false;
    tun_device->ReadFrame().ThenExactlyOnce(
        [&done, &expect](fidl::WireUnownedResult<tun::Device::ReadFrame>& call_result) {
          if (!call_result.ok()) {
            ADD_FAILURE() << "ReadFrame failed: " << call_result.error();
            return;
          }
          done = true;
          const fit::result<zx_status_t, ::fuchsia_net_tun::wire::DeviceReadFrameResponse*>*
              result = call_result.Unwrap();
          if (result->is_error()) {
            ADD_FAILURE() << "Read frame failed " << zx_status_get_string(result->error_value());
          }
          auto& frame = result->value()->frame;
          ASSERT_EQ(frame.frame_type(), netdev::wire::FrameType::kEthernet);
          ASSERT_TRUE(std::equal(std::begin(frame.data()), std::end(frame.data()), expect.begin(),
                                 expect.end()));
        });
    ASSERT_TRUE(RunLoopUntilOrFailure([&done]() { return done; }));
  }
}

TEST_F(NetDeviceTest, TestPortInfoNoMac) {
  // Create the tun device and client.
  auto tun_device_result = OpenTunDeviceAndPort();
  ASSERT_OK(tun_device_result.status_value());
  auto& [tun_device, tun_port, port_id] = tun_device_result.value();
  std::unique_ptr<NetworkDeviceClient> client;
  ASSERT_OK(tun_device->GetDevice(CreateClientRequest(&client)).status());

  // Fetch the port's details.
  std::optional<zx::status<network::client::PortInfoAndMac>> response;
  client->GetPortInfoWithMac(port_id,
                             [&response](zx::status<network::client::PortInfoAndMac> result) {
                               response = std::move(result);
                             });
  ASSERT_TRUE(RunLoopUntilOrFailure([&response]() { return response.has_value(); }));

  // Ensure the values are correct.
  ASSERT_OK(response->status_value());
  const network::client::PortInfoAndMac& port_details = response->value();
  EXPECT_EQ(port_details.id.base, port_id.base);
  EXPECT_EQ(port_details.id.salt, port_id.salt);
  ASSERT_EQ(port_details.rx_types.size(), 1u);
  EXPECT_EQ(port_details.rx_types.at(0), netdev::wire::FrameType::kEthernet);
  ASSERT_EQ(port_details.tx_types.size(), 1u);
  EXPECT_EQ(port_details.tx_types.at(0).type, netdev::wire::FrameType::kEthernet);
  EXPECT_EQ(port_details.unicast_address, std::nullopt);
}

TEST_F(NetDeviceTest, TestPortInfoWithMac) {
  static constexpr fuchsia_net::wire::MacAddress kMacAddress = {
      .octets = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66},
  };

  // Setup up the tun port's mac address.
  tun::wire::DevicePortConfig config = DefaultDevicePortConfig();
  config.set_mac(alloc_, kMacAddress);

  // Create the tun device and client.
  auto tun_device_result = OpenTunDeviceAndPort(DefaultDeviceConfig(), config);
  ASSERT_OK(tun_device_result.status_value());
  auto& [tun_device, tun_port, port_id] = tun_device_result.value();
  std::unique_ptr<NetworkDeviceClient> client;
  ASSERT_OK(tun_device->GetDevice(CreateClientRequest(&client)).status());

  // Fetch the port's details.
  std::optional<zx::status<network::client::PortInfoAndMac>> response;
  client->GetPortInfoWithMac(port_id,
                             [&response](zx::status<network::client::PortInfoAndMac> result) {
                               response = std::move(result);
                             });
  ASSERT_TRUE(RunLoopUntilOrFailure([&response]() { return response.has_value(); }));

  // Verify the unicast address was correctly fetched.
  ASSERT_OK(response->status_value());
  const network::client::PortInfoAndMac& port_details = response->value();
  ASSERT_TRUE(port_details.unicast_address.has_value());
  fidl::Array expected_mac = kMacAddress.octets;
  fidl::Array actual_mac = port_details.unicast_address.value().octets;
  EXPECT_EQ(std::basic_string_view(expected_mac.data(), expected_mac.size()),
            std::basic_string_view(actual_mac.data(), actual_mac.size()));
}

TEST_F(NetDeviceTest, TestPortInfoInvalidPort) {
  // Create the tun device and client.
  auto tun_device_result = OpenTunDeviceAndPort();
  ASSERT_OK(tun_device_result.status_value());
  auto& [tun_device, tun_port, port_id] = tun_device_result.value();
  std::unique_ptr<NetworkDeviceClient> client;
  ASSERT_OK(tun_device->GetDevice(CreateClientRequest(&client)).status());

  // Query an invalid port.
  std::optional<zx::status<network::client::PortInfoAndMac>> response;
  client->GetPortInfoWithMac(netdev::wire::PortId{.base = 17},
                             [&response](zx::status<network::client::PortInfoAndMac> result) {
                               response = std::move(result);
                             });
  ASSERT_TRUE(RunLoopUntilOrFailure([&response]() { return response.has_value(); }));

  // Ensure we received the correct error.
  ASSERT_STATUS(response->status_value(), ZX_ERR_NOT_FOUND);
}

// Guards against a regression where in-flight tx and rx frames could cause a
// page fault when the device is closed.
TEST_F(NetDeviceTest, CancelsWaitOnTeardown) {
  auto tun_device_result = OpenTunDeviceAndPort();
  ASSERT_OK(tun_device_result.status_value());
  auto& [tun_device, tun_port, port_id] = tun_device_result.value();
  std::unique_ptr<NetworkDeviceClient> client;
  ASSERT_OK(tun_device->GetDevice(CreateClientRequest(&client)).status());

  ASSERT_OK(StartSession(*client));
  ASSERT_OK(AttachPort(*client, port_id));

  // Generate some rx and tx traffic.
  for (size_t i = 0; i < 10; i++) {
    // NB: Not a constant because FromExternal doesn't like constant pointers.
    uint8_t kSendData[] = {1, 2, 3};
    tun::wire::Frame frame(alloc_);
    frame.set_frame_type(netdev::wire::FrameType::kEthernet);
    frame.set_data(alloc_,
                   fidl::VectorView<uint8_t>::FromExternal(kSendData, std::size(kSendData)));
    frame.set_port(kPortId);
    tun_device->WriteFrame(frame).ThenExactlyOnce(
        [](fidl::WireUnownedResult<tun::Device::WriteFrame>& call_result) {
          if (!call_result.ok()) {
            if (call_result.is_canceled()) {
              // Expected error.
              return;
            }
            ADD_FAILURE() << "WriteFrame failed: " << call_result.error();
            return;
          }
          const fit::result<zx_status_t>* result = call_result.Unwrap();
          zx_status_t status = [&result]() {
            if (result->is_error()) {
              return result->error_value();
            }
            return ZX_OK;
          }();
          EXPECT_OK(status);
        });
    auto tx = client->AllocTx();
    ASSERT_TRUE(tx.is_valid());
    tx.data().SetFrameType(fuchsia_hardware_network::wire::FrameType::kEthernet);
    tx.data().SetPortId(port_id);
    ASSERT_EQ(tx.data().Write(kSendData, std::size(kSendData)), std::size(kSendData));
    ASSERT_OK(tx.Send());
  }

  std::optional<zx_status_t> err;
  client->SetErrorCallback([&err](zx_status_t status) { err = status; });
  // Drop the device, wait for termination, then run the loop until idle.
  tun_port.AsyncTeardown();
  tun_device.AsyncTeardown();
  ASSERT_TRUE(RunLoopUntilOrFailure([&err]() { return err.has_value(); }));
  ASSERT_STATUS(err.value(), ZX_ERR_PEER_CLOSED);
  RunLoopUntilIdle();
}

struct SessionConfigTestCase {
  std::string name;
  uint32_t max_buffer_length;
  uint32_t alignment;
  uint64_t expected_buffer_stride;
  uint16_t min_tx_buffer_head;
  uint16_t min_tx_buffer_tail;
  zx_status_t expected_config_validity;
};

SessionConfigTestCase config_test_cases[] = {
    {
        .name = "DefaultBufferSizeWithDefaultAlignment",
        .max_buffer_length = NetworkDeviceClient::kDefaultBufferLength,
        .alignment = 1,
        .expected_buffer_stride = uint64_t{NetworkDeviceClient::kDefaultBufferLength},
        .min_tx_buffer_head = 0,
        .min_tx_buffer_tail = 0,
        .expected_config_validity = ZX_OK,
    },
    {
        .name = "BufferSizeIsMultipleOfAlignment",
        .max_buffer_length = 256,
        .alignment = 8,
        .expected_buffer_stride = uint64_t{256},
        .min_tx_buffer_head = 0,
        .min_tx_buffer_tail = 0,
        .expected_config_validity = ZX_OK,
    },
    {
        .name = "AlignUpWhenBufferSizeIsNotMultipleOfAlignment",
        .max_buffer_length = 64 + 112,
        .alignment = 64,
        .expected_buffer_stride = uint64_t{64 + 128},
        .min_tx_buffer_head = 0,
        .min_tx_buffer_tail = 0,
        .expected_config_validity = ZX_OK,
    },
    {
        .name = "BufferSizeSmallerThanAlignment",
        .max_buffer_length = 64,
        .alignment = 128,
        .expected_buffer_stride = uint64_t{128},
        .min_tx_buffer_head = 0,
        .min_tx_buffer_tail = 0,
        .expected_config_validity = ZX_OK,
    },
    {
        .name = "SessionBufferLengthFitsHeaderTail",
        .max_buffer_length = NetworkDeviceClient::kDefaultBufferLength,
        .alignment = 128,
        .expected_buffer_stride = NetworkDeviceClient::kDefaultBufferLength,
        .min_tx_buffer_head = 32,
        .min_tx_buffer_tail = 32,
        .expected_config_validity = ZX_OK,
    },
    {
        .name = "SessionBufferLengthTooShort",
        .max_buffer_length = 64,
        .alignment = 64,
        .expected_buffer_stride = 64,
        .min_tx_buffer_head = 32,
        .min_tx_buffer_tail = 32,
        .expected_config_validity = ZX_ERR_INVALID_ARGS,
    },
};

class SessionConfigTest : public testing::TestWithParam<SessionConfigTestCase> {};

TEST_P(SessionConfigTest, GeneratesCorrectSessionConfig) {
  const auto params = GetParam();
  auto config = NetworkDeviceClient::DefaultSessionConfig({
      // These values are copied from the default network tun device.
      .min_descriptor_length = 0,
      .descriptor_version = 0,
      .rx_depth = 255,
      .tx_depth = 255,
      .buffer_alignment = params.alignment,
      .max_buffer_length = params.max_buffer_length,
      .min_rx_buffer_length = 0,
      .min_tx_buffer_length = 0,
      .min_tx_buffer_head = params.min_tx_buffer_head,
      .min_tx_buffer_tail = params.min_tx_buffer_tail,
      .max_buffer_parts = 255,
  });
  EXPECT_EQ(config.buffer_stride, params.expected_buffer_stride);
  EXPECT_EQ(config.tx_header_length, params.min_tx_buffer_head);
  EXPECT_EQ(config.tx_tail_length, params.min_tx_buffer_tail);
  EXPECT_STATUS(config.Validate(), params.expected_config_validity);
}

INSTANTIATE_TEST_SUITE_P(SessionConfigSuite, SessionConfigTest,
                         testing::ValuesIn<SessionConfigTestCase>(config_test_cases),
                         [](const testing::TestParamInfo<SessionConfigTest::ParamType>& info) {
                           return info.param.name;
                         });

class GetPortsTest : public NetDeviceTest, public testing::WithParamInterface<size_t> {};

TEST_P(GetPortsTest, GetPortsWithPortCount) {
  const size_t port_count = GetParam();
  auto tun_device_result = OpenTunDevice(DefaultDeviceConfig());
  ASSERT_OK(tun_device_result.status_value());
  fidl::ClientEnd tun_device = std::move(tun_device_result.value());
  std::unique_ptr<NetworkDeviceClient> client;
  ASSERT_OK(fidl::WireCall(tun_device)->GetDevice(CreateClientRequest(&client)).status());

  // Sidestep having to write a custom hasher to put things in a set.
  union PortId {
    netdev::wire::PortId id;
    uint16_t v;
  };
  static_assert(sizeof(netdev::wire::PortId) == sizeof(uint16_t));

  std::vector<fidl::ClientEnd<tun::Port>> ports;
  std::unordered_set<uint16_t> expect_ids;
  for (size_t i = 0; i < port_count; i++) {
    SCOPED_TRACE(i);
    tun::wire::DevicePortConfig config = DefaultDevicePortConfig();
    config.base().set_id(static_cast<uint8_t>(i + 1));
    zx::status status = AddTunPort(tun_device, std::move(config));
    ASSERT_OK(status.status_value());
    fidl::ClientEnd port = std::move(status.value());
    zx::status endpoints = fidl::CreateEndpoints<netdev::Port>();
    ASSERT_OK(endpoints.status_value());
    auto [client, server] = std::move(endpoints.value());
    ASSERT_OK(fidl::WireCall(port)->GetPort(std::move(server)).status());
    ports.push_back(std::move(port));

    fidl::WireResult result = fidl::WireCall(client)->GetInfo();
    ASSERT_OK(result.status());
    const netdev::wire::PortInfo& info = result.value().info;
    ASSERT_TRUE(info.has_id());
    expect_ids.insert(PortId{.id = info.id()}.v);
  }

  std::optional<zx::status<std::vector<netdev::wire::PortId>>> response;
  client->GetPorts(
      [&response](zx::status<std::vector<netdev::wire::PortId>> result) { response = result; });
  ASSERT_TRUE(RunLoopUntilOrFailure([&response]() { return response.has_value(); }));
  zx::status status = std::move(response.value());
  ASSERT_OK(status.status_value());
  std::vector<netdev::wire::PortId> got_ids = std::move(status.value());
  EXPECT_EQ(got_ids.size(), expect_ids.size());
  for (netdev::wire::PortId id : got_ids) {
    auto it = expect_ids.find(PortId{.id = id}.v);
    EXPECT_NE(it, expect_ids.end())
        << " couldn't find ID " << id.base << ", " << id.salt << " in set ";
    expect_ids.erase(it);
  }
}

INSTANTIATE_TEST_SUITE_P(NetDeviceTest, GetPortsTest, testing::Values(0, 1, 3),
                         [](const testing::TestParamInfo<size_t>& info) {
                           return std::to_string(info.param);
                         });

}  // namespace
