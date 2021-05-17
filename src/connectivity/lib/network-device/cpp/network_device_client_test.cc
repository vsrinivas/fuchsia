// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/lib/network-device/cpp/network_device_client.h"

#include <fuchsia/net/tun/llcpp/fidl.h>
#include <lib/fit/bridge.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/service/llcpp/service.h>
#include <lib/zx/time.h>
#include <zircon/status.h>

#include <memory>

#include <fbl/unique_fd.h>

#include "src/lib/testing/predicates/status.h"

namespace {

// Enable timeouts only to test things locally, committed code should not use timeouts.
constexpr zx::duration kTimeout = zx::duration::infinite();

using namespace network::client;

namespace tun = fuchsia_net_tun;

template <class T>
class TestEventHandler : public fidl::WireAsyncEventHandler<T> {
 public:
  TestEventHandler(const char* name) : name_(name) {}

  virtual void Unbound(fidl::UnbindInfo info) override {
    if (!info.ok()) {
      FAIL() << "Lost connection to " << name_ << ": " << info;
    }
  }

 private:
  const char* name_;
};

class NetDeviceTest : public gtest::RealLoopFixture {
 public:
  NetDeviceTest() : gtest::RealLoopFixture() {}

  bool RunLoopUntilOrFailure(fit::function<bool()> f) {
    return RunLoopWithTimeoutOrUntil([f = std::move(f)] { return HasFailure() || f(); }, kTimeout,
                                     zx::duration::infinite());
  }

  tun::wire::BaseConfig DefaultBaseConfig() {
    tun::wire::BaseConfig config(alloc_);
    config.set_mtu(alloc_, 1500);
    const netdev::wire::FrameType rx_types[] = {
        netdev::wire::FrameType::kEthernet,
    };
    fidl::VectorView<netdev::wire::FrameType> rx_types_view(alloc_, std::size(rx_types));
    std::copy(std::begin(rx_types), std::end(rx_types), rx_types_view.mutable_data());
    const netdev::wire::FrameTypeSupport tx_types[] = {
        netdev::wire::FrameTypeSupport{
            .type = netdev::wire::FrameType::kEthernet,
            .features = netdev::wire::kFrameFeaturesRaw,
        },
    };
    fidl::VectorView<netdev::wire::FrameTypeSupport> tx_types_view(alloc_, std::size(tx_types));
    std::copy(std::begin(tx_types), std::end(tx_types), tx_types_view.mutable_data());
    config.set_rx_types(alloc_, rx_types_view);
    config.set_tx_types(alloc_, tx_types_view);
    return config;
  }

  tun::wire::DeviceConfig DefaultDeviceConfig() {
    tun::wire::DeviceConfig config(alloc_);
    config.set_base(alloc_, DefaultBaseConfig());
    config.set_online(alloc_, true);
    config.set_blocking(alloc_, true);
    return config;
  }

  tun::wire::DevicePairConfig DefaultPairConfig() {
    tun::wire::DevicePairConfig config(alloc_);
    config.set_base(alloc_, DefaultBaseConfig());
    return config;
  }

  zx::status<fidl::Client<tun::Device>> OpenTunDevice(tun::wire::DeviceConfig config) {
    zx::status device_endpoints = fidl::CreateEndpoints<tun::Device>();
    if (device_endpoints.is_error()) {
      return device_endpoints.take_error();
    }
    zx::status tunctl = service::Connect<tun::Control>();
    if (tunctl.is_error()) {
      return tunctl.take_error();
    }
    fidl::WireSyncClient tun = fidl::BindSyncClient(std::move(tunctl.value()));
    if (zx_status_t status =
            tun.CreateDevice(std::move(config), std::move(device_endpoints->server)).status();
        status != ZX_OK) {
      return zx::error(status);
    }
    return zx::ok(fidl::Client(std::move(device_endpoints->client), dispatcher(),
                               std::make_shared<TestEventHandler<tun::Device>>("tun device")));
  }

  zx::status<fidl::Client<tun::Device>> OpenTunDevice() {
    return OpenTunDevice(DefaultDeviceConfig());
  }

  zx::status<fidl::Client<tun::DevicePair>> OpenTunPair(tun::wire::DevicePairConfig config) {
    zx::status device_pair_endpoints = fidl::CreateEndpoints<tun::DevicePair>();
    if (device_pair_endpoints.is_error()) {
      return device_pair_endpoints.take_error();
    }
    zx::status tunctl = service::Connect<tun::Control>();
    if (tunctl.is_error()) {
      return tunctl.take_error();
    }
    fidl::WireSyncClient tun = fidl::BindSyncClient(std::move(tunctl.value()));
    if (zx_status_t status =
            tun.CreatePair(std::move(config), std::move(device_pair_endpoints->server)).status();
        status != ZX_OK) {
      return zx::error(status);
    }
    return zx::ok(
        fidl::Client(std::move(device_pair_endpoints->client), dispatcher(),
                     std::make_shared<TestEventHandler<tun::DevicePair>>("tun device pair")));
  }

  zx::status<fidl::Client<tun::DevicePair>> OpenTunPair() {
    return OpenTunPair(DefaultPairConfig());
  }

  static void WaitTapOnlineInner(fidl::Client<tun::Device>& tun_device,
                                 fit::callback<void()> complete) {
    tun_device->WatchState([&tun_device, complete = std::move(complete)](
                               fidl::WireResponse<tun::Device::WatchState>* response) mutable {
      if (response->state.has_session()) {
        complete();
      } else {
        WaitTapOnlineInner(tun_device, std::move(complete));
      }
    });
  }

  bool WaitTapOnline(fidl::Client<tun::Device>& tun_device) {
    bool online = false;
    WaitTapOnlineInner(tun_device, [&online]() { online = true; });
    if (!RunLoopUntilOrFailure([&online]() { return online; })) {
      return false;
    }
    return true;
  }

  tun::wire::Protocols CreateClientRequest(std::unique_ptr<NetworkDeviceClient>* out_client) {
    zx::status device_endpoints = fidl::CreateEndpoints<fuchsia_hardware_network::Device>();
    EXPECT_OK(device_endpoints.status_value());
    tun::wire::Protocols protos(alloc_);
    protos.set_network_device(alloc_, std::move(device_endpoints->server));
    std::unique_ptr client =
        std::make_unique<NetworkDeviceClient>(std::move(device_endpoints->client));
    client->SetErrorCallback([](zx_status_t error) {
      FAIL() << "Client experienced error " << zx_status_get_string(error);
    });
    *out_client = std::move(client);
    return protos;
  }

  zx_status_t StartSession(NetworkDeviceClient& client) {
    std::optional<zx_status_t> opt;
    client.OpenSession("netdev_unittest", [&opt](zx_status_t status) { opt = status; });
    if (!RunLoopUntilOrFailure([&opt]() { return opt.has_value(); })) {
      return ZX_ERR_TIMED_OUT;
    }
    return opt.value();
  }

 protected:
  fidl::FidlAllocator<> alloc_;
};

TEST_F(NetDeviceTest, TestRxTx) {
  auto tun_device_result = OpenTunDevice();
  ASSERT_OK(tun_device_result.status_value());
  auto& tun_device = tun_device_result.value();
  std::unique_ptr<NetworkDeviceClient> client;
  tun_device->ConnectProtocols(CreateClientRequest(&client));

  ASSERT_OK(StartSession(*client));
  ASSERT_OK(client->SetPaused(false));
  ASSERT_TRUE(WaitTapOnline(tun_device));
  ASSERT_TRUE(client->HasSession());

  bool done = false;
  std::vector<uint8_t> send_data({0x01, 0x02, 0x03});
  client->SetRxCallback([&done, &send_data](NetworkDeviceClient::Buffer buffer) {
    done = true;
    auto& data = buffer.data();
    ASSERT_EQ(data.frame_type(), fuchsia_hardware_network::wire::FrameType::kEthernet);
    ASSERT_EQ(data.len(), send_data.size());
    ASSERT_EQ(data.parts(), 1u);
    ASSERT_EQ(memcmp(&send_data[0], &data.part(0).data()[0], data.len()), 0);
  });

  bool wrote_frame = false;
  tun::wire::Frame frame(alloc_);
  frame.set_frame_type(alloc_, netdev::wire::FrameType::kEthernet);
  frame.set_data(alloc_, fidl::VectorView<uint8_t>::FromExternal(send_data));
  tun_device->WriteFrame(
      std::move(frame), [&wrote_frame](fidl::WireResponse<tun::Device::WriteFrame>* response) {
        const tun::wire::DeviceWriteFrameResult& result = response->result;
        wrote_frame = true;
        switch (result.which()) {
          case tun::wire::DeviceWriteFrameResult::Tag::kErr:
            FAIL() << "Failed to write to device " << zx_status_get_string(result.err());
            break;
          case tun::wire::DeviceWriteFrameResult::Tag::kResponse:
            break;
        }
      });
  ASSERT_TRUE(RunLoopUntilOrFailure([&done, &wrote_frame]() { return done && wrote_frame; }))
      << "Timed out waiting for frame; done=" << done << ", wrote_frame=" << wrote_frame;

  done = false;
  tun_device->ReadFrame([&done, &send_data](fidl::WireResponse<tun::Device::ReadFrame>* response) {
    done = true;
    const tun::wire::DeviceReadFrameResult& result = response->result;
    switch (result.which()) {
      case tun::wire::DeviceReadFrameResult::Tag::kErr:
        FAIL() << "Failed to read from device " << zx_status_get_string(result.err());
        break;
      case tun::wire::DeviceReadFrameResult::Tag::kResponse:
        ASSERT_EQ(result.response().frame.frame_type(), netdev::wire::FrameType::kEthernet);
        const fidl::VectorView<uint8_t>& data = result.response().frame.data();
        ASSERT_TRUE(
            std::equal(std::begin(data), std::end(data), send_data.begin(), send_data.end()));
        break;
    }
  });

  auto tx = client->AllocTx();
  ASSERT_TRUE(tx.is_valid());
  tx.data().SetFrameType(fuchsia_hardware_network::wire::FrameType::kEthernet);
  ASSERT_EQ(tx.data().Write(&send_data[0], send_data.size()), send_data.size());
  ASSERT_OK(tx.Send());

  ASSERT_TRUE(RunLoopUntilOrFailure([&done]() { return done; }));
}

TEST_F(NetDeviceTest, TestEcho) {
  auto tun_device_result = OpenTunDevice();
  ASSERT_OK(tun_device_result.status_value());
  auto& tun_device = tun_device_result.value();
  std::unique_ptr<NetworkDeviceClient> client;
  tun_device->ConnectProtocols(CreateClientRequest(&client));

  ASSERT_OK(StartSession(*client));
  ASSERT_OK(client->SetPaused(false));
  ASSERT_TRUE(WaitTapOnline(tun_device));
  ASSERT_TRUE(client->HasSession());

  constexpr uint32_t kTestFrames = 128;
  fit::function<void()> write_frame;
  uint32_t frame_count = 0;
  fit::bridge<void, zx_status_t> write_bridge;
  write_frame = [this, &frame_count, &tun_device, &write_bridge, &write_frame]() {
    if (frame_count == kTestFrames) {
      write_bridge.completer.complete_ok();
    } else {
      tun::wire::Frame frame(alloc_);
      frame.set_frame_type(alloc_, netdev::wire::FrameType::kEthernet);
      frame.set_data(alloc_, fidl::VectorView<uint8_t>::FromExternal(
                                 reinterpret_cast<uint8_t*>(&frame_count), sizeof(frame_count)));
      tun_device->WriteFrame(
          std::move(frame),
          [&write_bridge, &write_frame](fidl::WireResponse<tun::Device::WriteFrame>* response) {
            const tun::wire::DeviceWriteFrameResult& result = response->result;
            switch (result.which()) {
              case tun::wire::DeviceWriteFrameResult::Tag::kErr:
                write_bridge.completer.complete_error(result.err());
                break;
              case tun::wire::DeviceWriteFrameResult::Tag::kResponse:
                // Write another frame.
                write_frame();
                break;
            }
          });
      frame_count++;
    }
  };

  uint32_t echoed = 0;
  client->SetRxCallback([&client, &echoed](NetworkDeviceClient::Buffer buffer) {
    echoed++;
    // Alternate between echoing with a copy and just descriptor swap.
    if (echoed % 2 == 0) {
      auto tx = client->AllocTx();
      ASSERT_TRUE(tx.is_valid()) << "Tx alloc failed at echo " << echoed;
      tx.data().SetFrameType(buffer.data().frame_type());
      tx.data().SetTxRequest(fuchsia_hardware_network::wire::TxFlags());
      auto wr = tx.data().Write(buffer.data());
      EXPECT_EQ(wr, buffer.data().len());
      EXPECT_EQ(tx.Send(), ZX_OK);
    } else {
      buffer.data().SetTxRequest(fuchsia_hardware_network::wire::TxFlags());
      EXPECT_EQ(buffer.Send(), ZX_OK);
    }
  });
  write_frame();
  fit::result write_result = RunPromise(write_bridge.consumer.promise());
  ASSERT_TRUE(write_result.is_ok())
      << "WriteFrame error: " << zx_status_get_string(write_result.error());

  uint32_t waiting = 0;
  fit::function<void()> receive_frame;
  fit::bridge<void, zx_status_t> read_bridge;
  receive_frame = [&waiting, &tun_device, &read_bridge, &receive_frame]() {
    tun_device->ReadFrame([&read_bridge, &receive_frame,
                           &waiting](fidl::WireResponse<tun::Device::ReadFrame>* response) {
      const tun::wire::DeviceReadFrameResult& result = response->result;
      switch (result.which()) {
        case tun::wire::DeviceReadFrameResult::Tag::kErr:
          read_bridge.completer.complete_error(result.err());
          break;
        case tun::wire::DeviceReadFrameResult::Tag::kResponse:
          EXPECT_EQ(result.response().frame.frame_type(), netdev::wire::FrameType::kEthernet);
          EXPECT_FALSE(result.response().frame.has_meta());
          if (size_t count = result.response().frame.data().count(); count != sizeof(uint32_t)) {
            ADD_FAILURE() << "Unexpected data size " << count;
          } else {
            uint32_t payload;
            memcpy(&payload, result.response().frame.data().data(), sizeof(uint32_t));
            EXPECT_EQ(payload, waiting);
          }
          waiting++;
          if (waiting == kTestFrames) {
            read_bridge.completer.complete_ok();
          } else {
            receive_frame();
          }
          break;
      }
    });
  };
  receive_frame();
  fit::result read_result = RunPromise(read_bridge.consumer.promise());
  ASSERT_TRUE(read_result.is_ok())
      << "ReadFrame failed " << zx_status_get_string(read_result.error());
  EXPECT_EQ(echoed, frame_count);
}

TEST_F(NetDeviceTest, TestEchoPair) {
  auto tun_pair_result = OpenTunPair();
  ASSERT_OK(tun_pair_result.status_value());
  auto& tun_pair = tun_pair_result.value();
  std::unique_ptr<NetworkDeviceClient> left, right;
  tun::wire::DevicePairEnds ends(alloc_);
  ends.set_left(alloc_, CreateClientRequest(&left));
  ends.set_right(alloc_, CreateClientRequest(&right));
  tun_pair->ConnectProtocols(std::move(ends));

  ASSERT_OK(StartSession(*left));
  ASSERT_OK(StartSession(*right));
  ASSERT_OK(left->SetPaused(false));
  ASSERT_OK(right->SetPaused(false));

  left->SetRxCallback([&left](NetworkDeviceClient::Buffer buffer) {
    auto tx = left->AllocTx();
    ASSERT_TRUE(tx.is_valid()) << "Tx alloc failed at echo";
    tx.data().SetFrameType(buffer.data().frame_type());
    tx.data().SetTxRequest(fuchsia_hardware_network::wire::TxFlags());
    uint32_t pload;
    EXPECT_EQ(buffer.data().Read(&pload, sizeof(pload)), sizeof(pload));
    pload = ~pload;
    EXPECT_EQ(tx.data().Write(&pload, sizeof(pload)), sizeof(pload));
    EXPECT_EQ(tx.Send(), ZX_OK);
  });

  constexpr uint32_t kBufferCount = 128;
  uint32_t rx_counter = 0;

  fit::bridge completed_bridge;
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
    fit::bridge online_bridge;
    auto status_handle =
        right->WatchStatus([completer = std::move(online_bridge.completer)](
                               fuchsia_hardware_network::wire::Status status) mutable {
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
    ASSERT_EQ(tx.data().Write(&i, sizeof(i)), sizeof(i));
    ASSERT_OK(tx.Send());
  }

  ASSERT_TRUE(RunPromise(completed_bridge.consumer.promise()).is_ok());
}

TEST_F(NetDeviceTest, StatusWatcher) {
  auto tun_device = OpenTunDevice();
  std::unique_ptr<NetworkDeviceClient> client;
  tun_device->ConnectProtocols(CreateClientRequest(&client));
  uint32_t call_count1 = 0;
  uint32_t call_count2 = 0;
  bool expect_online = true;

  auto watcher1 = client->WatchStatus([&call_count1, &expect_online](
                                          fuchsia_hardware_network::wire::Status status) {
    call_count1++;
    ASSERT_EQ(
        static_cast<bool>(status.flags() & fuchsia_hardware_network::wire::StatusFlags::kOnline),
        expect_online)
        << "Unexpected status flags " << static_cast<uint32_t>(status.flags())
        << ", online should be " << expect_online;
  });

  {
    auto watcher2 = client->WatchStatus(
        [&call_count2](fuchsia_hardware_network::wire::Status status) { call_count2++; });

    // Run loop with both watchers attached.
    ASSERT_TRUE(RunLoopUntilOrFailure([&call_count1, &call_count2]() {
      return call_count1 == 1 && call_count2 == 1;
    })) << "call_count1="
        << call_count1 << ", call_count2=" << call_count2;

    // Set online to false and wait for both watchers again.
    ASSERT_OK(tun_device->SetOnline_Sync(false).status());
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
    ASSERT_OK(tun_device->SetOnline_Sync(expect_online).status());
    ASSERT_TRUE(RunLoopUntilOrFailure([&call_count1, &i]() { return call_count1 == 3 + i; }))
        << "call_count1=" << call_count1 << ", call_count2=" << call_count2;
    // call_count2 mustn't change.
    ASSERT_EQ(call_count2, 2u);
  }
}

TEST_F(NetDeviceTest, ErrorCallback) {
  auto tun_device_result = OpenTunDevice();
  ASSERT_OK(tun_device_result.status_value());
  auto& tun_device = tun_device_result.value();
  std::unique_ptr<NetworkDeviceClient> client;
  tun_device->ConnectProtocols(CreateClientRequest(&client));

  ASSERT_OK(StartSession(*client));
  ASSERT_OK(client->SetPaused(false));
  ASSERT_TRUE(WaitTapOnline(tun_device));
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
  constexpr size_t kMinBufferLength = sizeof(kPayload) - 2;
  constexpr size_t kSmallPayloadLength = kMinBufferLength - 2;
  auto device_config = DefaultDeviceConfig();
  device_config.base().set_min_tx_buffer_length(alloc_, kMinBufferLength);
  auto tun_device_result = OpenTunDevice(std::move(device_config));
  ASSERT_OK(tun_device_result.status_value());
  auto& tun_device = tun_device_result.value();
  std::unique_ptr<NetworkDeviceClient> client;
  tun_device->ConnectProtocols(CreateClientRequest(&client));

  ASSERT_OK(StartSession(*client));
  ASSERT_OK(client->SetPaused(false));
  ASSERT_TRUE(WaitTapOnline(tun_device));
  ASSERT_TRUE(client->HasSession());

  // Send three frames: one too small, one exactly minimum length, and one larger than minimum
  // length.
  for (auto& frame : {
           fbl::Span(kPayload, sizeof(kSmallPayloadLength)),
           fbl::Span(kPayload, sizeof(kMinBufferLength)),
           fbl::Span(kPayload),
       }) {
    auto tx = client->AllocTx();
    // Pollute buffer data first to check zero-padding.
    for (auto& b : tx.data().part(0).data()) {
      b = 0xAA;
    }
    ASSERT_TRUE(tx.is_valid());
    tx.data().SetFrameType(fuchsia_hardware_network::wire::FrameType::kEthernet);
    EXPECT_EQ(tx.data().Write(frame.data(), frame.size()), frame.size());
    EXPECT_EQ(tx.Send(), ZX_OK);

    std::vector<uint8_t> expect(frame.begin(), frame.end());
    while (expect.size() < kMinBufferLength) {
      expect.push_back(0);
    }

    // Retrieve the frame and assert it's what we expect.
    bool done = false;
    tun_device->ReadFrame([&done, &expect](fidl::WireResponse<tun::Device::ReadFrame>* response) {
      done = true;
      const tun::wire::DeviceReadFrameResult& result = response->result;
      switch (result.which()) {
        case tun::wire::DeviceReadFrameResult::Tag::kErr:
          ADD_FAILURE() << "Read frame failed " << zx_status_get_string(result.err());
          break;
        case tun::wire::DeviceReadFrameResult::Tag::kResponse:
          auto& frame = result.response().frame;
          ASSERT_EQ(frame.frame_type(), netdev::wire::FrameType::kEthernet);
          ASSERT_TRUE(std::equal(std::begin(frame.data()), std::end(frame.data()), expect.begin(),
                                 expect.end()));
          break;
      }
    });
    ASSERT_TRUE(RunLoopUntilOrFailure([&done]() { return done; }));
  }
}

}  // namespace
