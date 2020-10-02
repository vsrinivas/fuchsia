// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/lib/network-device/cpp/network_device_client.h"

#include <fuchsia/net/tun/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fit/bridge.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/clock.h>
#include <zircon/status.h>

#include <memory>

#include <fbl/unique_fd.h>

#include "src/lib/testing/predicates/status.h"

namespace {

// Enable timeouts only to test things locally, committed code should not use timeouts.
constexpr zx::duration kTimeout = zx::duration::infinite();

using namespace network::client;

namespace tun = fuchsia::net::tun;

class NetDeviceTest : public gtest::RealLoopFixture {
 public:
  NetDeviceTest() : gtest::RealLoopFixture(), executor_(dispatcher()) {}

  static fidl::InterfaceHandle<tun::Control> ConnectTunCtl() {
    fidl::InterfaceHandle<tun::Control> ret;
    sys::ServiceDirectory::CreateFromNamespace()->Connect(ret.NewRequest());
    return ret;
  }

  bool RunLoopUntilOrFailure(fit::function<bool()> f) {
    return RunLoopWithTimeoutOrUntil([f = std::move(f)] { return HasFailure() || f(); }, kTimeout,
                                     zx::duration::infinite());
  }

  template <typename PromiseType>
  typename PromiseType::result_type RunPromise(PromiseType promise) {
    typename PromiseType::result_type r;
    executor_.schedule_task(
        promise.then([&r](typename PromiseType::result_type& result) { r = std::move(result); }));
    if (!RunLoopUntilOrFailure([&r] { return !r.is_pending(); })) {
      ADD_FAILURE() << "Timed out waiting for promise to complete";
    }
    return r;
  }

  static tun::BaseConfig DefaultBaseConfig() {
    tun::BaseConfig config;
    config.set_mtu(1500);
    config.set_rx_types({netdev::FrameType::ETHERNET});
    config.set_tx_types({netdev::FrameTypeSupport{netdev::FrameType::ETHERNET,
                                                  netdev::FRAME_FEATURES_RAW, netdev::TxFlags()}});
    return config;
  }

  static tun::DeviceConfig DefaultDeviceConfig() {
    tun::DeviceConfig config;
    config.set_online(true);
    config.set_blocking(true);
    config.set_base(DefaultBaseConfig());
    return config;
  }

  static tun::DevicePairConfig DefaultPairConfig() {
    tun::DevicePairConfig config;
    config.set_base(DefaultBaseConfig());
    return config;
  }

  tun::DevicePtr OpenTunDevice(tun::DeviceConfig config = DefaultDeviceConfig()) {
    tun::ControlPtr tunctl = ConnectTunCtl().Bind();
    tunctl.set_error_handler([](zx_status_t err) {
      FAIL() << "Lost connection to tunctl " << zx_status_get_string(err);
    });

    tun::DevicePtr tun_device;
    tun_device.set_error_handler([](zx_status_t err) {
      FAIL() << "Lost connection to tun device " << zx_status_get_string(err);
    });
    tun::DevicePtr device;
    tunctl->CreateDevice(std::move(config), device.NewRequest());
    return device;
  }

  tun::DevicePairPtr OpenTunPair() {
    tun::ControlPtr tunctl = ConnectTunCtl().Bind();
    tunctl.set_error_handler([](zx_status_t err) {
      FAIL() << "Lost connection to tunctl " << zx_status_get_string(err);
    });

    tun::DevicePtr tun_device;
    tun_device.set_error_handler([](zx_status_t err) {
      FAIL() << "Lost connection to tun device " << zx_status_get_string(err);
    });
    tun::DevicePairPtr pair_device;
    tunctl->CreatePair(DefaultPairConfig(), pair_device.NewRequest());
    return pair_device;
  }

  static void WaitTapOnline(tun::DevicePtr* tun_device, fit::callback<void()> complete) {
    (*tun_device)
        ->WatchState(
            [tun_device, complete = std::move(complete)](tun::InternalState state) mutable {
              if (state.has_session()) {
                complete();
              } else {
                WaitTapOnline(tun_device, std::move(complete));
              }
            });
  }

  static fit::promise<void, zx_status_t> TapOnlinePromise(tun::DevicePtr* tun_device) {
    fit::bridge<void, zx_status_t> bridge;
    WaitTapOnline(tun_device,
                  [completer = std::move(bridge.completer)]() mutable { completer.complete_ok(); });
    return bridge.consumer.promise();
  }

  static tun::Protocols CreateClientRequest(std::unique_ptr<NetworkDeviceClient>* client) {
    fidl::InterfaceHandle<netdev::Device> handle;
    tun::Protocols protos;
    protos.set_network_device(handle.NewRequest());
    *client = std::make_unique<NetworkDeviceClient>(std::move(handle));
    (*client)->SetErrorCallback([](zx_status_t error) {
      FAIL() << "Client experienced error " << zx_status_get_string(error);
    });
    return protos;
  }

  static fit::promise<void, zx_status_t> StartSessionPromise(NetworkDeviceClient* client) {
    fit::bridge<zx_status_t, void> bridge;
    client->OpenSession("netdev_unittest", bridge.completer.bind());
    return bridge.consumer.promise().then(
        [](fit::result<zx_status_t>& status) -> fit::result<void, zx_status_t> {
          if (status.is_ok()) {
            if (status.value() == ZX_OK) {
              return fit::ok();
            } else {
              return fit::error(status.value());
            }
          } else {
            return fit::error(ZX_ERR_INTERNAL);
          }
        });
  }

 protected:
  async::Executor executor_;
};

TEST_F(NetDeviceTest, TestRxTx) {
  auto tun_device = OpenTunDevice();
  std::unique_ptr<NetworkDeviceClient> client;
  tun_device->ConnectProtocols(CreateClientRequest(&client));

  auto startup = RunPromise(StartSessionPromise(client.get())
                                .and_then([&client]() { ASSERT_OK(client->SetPaused(false)); })
                                .and_then(TapOnlinePromise(&tun_device)));
  ASSERT_NO_FATAL_FAILURE();
  ASSERT_TRUE(startup.is_ok()) << "Session startup failed: " << startup.error();

  bool done = false;
  std::vector<uint8_t> send_data({0x01, 0x02, 0x03});
  client->SetRxCallback([&done, &send_data](NetworkDeviceClient::Buffer buffer) {
    done = true;
    auto& data = buffer.data();
    ASSERT_EQ(data.frame_type(), netdev::FrameType::ETHERNET);
    ASSERT_EQ(data.len(), send_data.size());
    ASSERT_EQ(data.parts(), 1u);
    ASSERT_EQ(memcmp(&send_data[0], &data.part(0).data()[0], data.len()), 0);
  });

  bool wrote_frame = false;
  tun::Frame frame;
  frame.set_frame_type(netdev::FrameType::ETHERNET);
  frame.set_data(send_data);
  tun_device->WriteFrame(std::move(frame), [&wrote_frame](fit::result<void, zx_status_t> status) {
    wrote_frame = true;
    ASSERT_TRUE(status.is_ok()) << "Failed to write to device "
                                << zx_status_get_string(status.error());
  });
  ASSERT_TRUE(RunLoopUntilOrFailure([&done, &wrote_frame]() { return done && wrote_frame; }))
      << "Timed out waiting for frame; done=" << done << ", wrote_frame=" << wrote_frame;
  ASSERT_NO_FATAL_FAILURE();

  done = false;
  tun_device->ReadFrame([&done, &send_data](tun::Device_ReadFrame_Result result) {
    done = true;
    if (result.is_response()) {
      ASSERT_EQ(result.response().frame.frame_type(), netdev::FrameType::ETHERNET);
      ASSERT_EQ(result.response().frame.data().size(), send_data.size());
      ASSERT_EQ(memcmp(&send_data[0], &result.response().frame.data()[0],
                       result.response().frame.data().size()),
                0);
    } else {
      ADD_FAILURE() << "Read frame failed " << zx_status_get_string(result.err());
    }
  });

  auto tx = client->AllocTx();
  ASSERT_TRUE(tx.is_valid());
  tx.data().SetFrameType(netdev::FrameType::ETHERNET);
  ASSERT_EQ(tx.data().Write(&send_data[0], send_data.size()), send_data.size());
  ASSERT_OK(tx.Send());

  ASSERT_TRUE(RunLoopUntilOrFailure([&done]() { return done; }));
}

TEST_F(NetDeviceTest, TestEcho) {
  auto tun_device = OpenTunDevice();
  std::unique_ptr<NetworkDeviceClient> client;
  tun_device->ConnectProtocols(CreateClientRequest(&client));

  auto startup = RunPromise(StartSessionPromise(client.get())
                                .and_then([&client]() { ASSERT_OK(client->SetPaused(false)); })
                                .and_then(TapOnlinePromise(&tun_device)));
  ASSERT_TRUE(startup.is_ok()) << "Session startup failed: " << startup.error();
  ASSERT_NO_FATAL_FAILURE();

  constexpr uint32_t kTestFrames = 128;
  fit::function<void()> write_frame;
  uint32_t frame_count = 0;
  fit::bridge<void, zx_status_t> write_bridge;
  write_frame = [&frame_count, &tun_device, &write_bridge, &write_frame]() {
    if (frame_count == kTestFrames) {
      write_bridge.completer.complete_ok();
    } else {
      const auto* ptr = reinterpret_cast<const uint8_t*>(&frame_count);
      std::vector<uint8_t> data(ptr, ptr + sizeof(uint32_t));
      tun::Frame frame;
      frame.set_frame_type(netdev::FrameType::ETHERNET);
      frame.set_data(std::vector<uint8_t>(ptr, ptr + sizeof(uint32_t)));
      tun_device->WriteFrame(std::move(frame),
                             [&write_bridge, &write_frame](fit::result<void, zx_status_t> status) {
                               if (status.is_ok()) {
                                 // write another frame
                                 write_frame();
                               } else {
                                 write_bridge.completer.complete_error(status.error());
                               }
                             });
      frame_count++;
    }
  };

  uint32_t echoed = 0;
  client->SetRxCallback([&client, &echoed](NetworkDeviceClient::Buffer buffer) {
    echoed++;
    // switch between echoing with a copy and just descriptor swap.
    if (echoed % 2 == 0) {
      auto tx = client->AllocTx();
      ASSERT_TRUE(tx.is_valid()) << "Tx alloc failed at echo " << echoed;
      tx.data().SetFrameType(buffer.data().frame_type());
      tx.data().SetTxRequest(netdev::TxFlags());
      auto wr = tx.data().Write(buffer.data());
      EXPECT_EQ(wr, buffer.data().len());
      EXPECT_EQ(tx.Send(), ZX_OK);
    } else {
      buffer.data().SetTxRequest(netdev::TxFlags());
      EXPECT_EQ(buffer.Send(), ZX_OK);
    }
  });
  write_frame();
  auto write_result = RunPromise(write_bridge.consumer.promise());
  ASSERT_NO_FATAL_FAILURE();
  ASSERT_TRUE(write_result.is_ok())
      << "WriteFrame error: " << zx_status_get_string(write_result.error());

  uint32_t waiting = 0;
  fit::function<void()> receive_frame;
  fit::bridge<void, zx_status_t> read_bridge;
  receive_frame = [&waiting, &tun_device, &read_bridge, &receive_frame]() {
    tun_device->ReadFrame(
        [&read_bridge, &receive_frame, &waiting](tun::Device_ReadFrame_Result result) {
          if (result.is_response()) {
            EXPECT_EQ(result.response().frame.frame_type(), netdev::FrameType::ETHERNET);
            EXPECT_FALSE(result.response().frame.has_meta());
            if (result.response().frame.data().size() == sizeof(uint32_t)) {
              uint32_t payload;
              memcpy(&payload, &result.response().frame.data()[0], sizeof(uint32_t));
              EXPECT_EQ(payload, waiting);
            } else {
              ADD_FAILURE() << "Unexpected data size " << result.response().frame.data().size();
            }
            waiting++;
            if (waiting == kTestFrames) {
              read_bridge.completer.complete_ok();
            } else {
              receive_frame();
            }
          } else {
            read_bridge.completer.complete_error(result.err());
          }
        });
  };
  receive_frame();
  auto read_result = RunPromise(read_bridge.consumer.promise());
  ASSERT_TRUE(read_result.is_ok())
      << "ReadFrame failed " << zx_status_get_string(read_result.error());
  ASSERT_NO_FATAL_FAILURE();
  EXPECT_EQ(echoed, frame_count);
}

TEST_F(NetDeviceTest, TestEchoPair) {
  auto tun_pair = OpenTunPair();
  std::unique_ptr<NetworkDeviceClient> left, right;
  tun::DevicePairEnds ends;
  ends.set_left(CreateClientRequest(&left));
  ends.set_right(CreateClientRequest(&right));
  tun_pair->ConnectProtocols(std::move(ends));
  auto start_result =
      RunPromise(StartSessionPromise(left.get()).and_then(StartSessionPromise(right.get())));
  ASSERT_TRUE(start_result.is_ok())
      << "Start session failure " << zx_status_get_string(start_result.error());
  ASSERT_OK(left->SetPaused(false));
  ASSERT_OK(right->SetPaused(false));

  left->SetRxCallback([&left](NetworkDeviceClient::Buffer buffer) {
    auto tx = left->AllocTx();
    ASSERT_TRUE(tx.is_valid()) << "Tx alloc failed at echo";
    tx.data().SetFrameType(buffer.data().frame_type());
    tx.data().SetTxRequest(netdev::TxFlags());
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
    EXPECT_EQ(buffer.data().frame_type(), netdev::FrameType::ETHERNET);
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
    auto status_handle = right->WatchStatus(
        [completer = std::move(online_bridge.completer)](netdev::Status status) mutable {
          if (static_cast<bool>(status.flags() & netdev::StatusFlags::ONLINE)) {
            completer.complete_ok();
          }
        });
    ASSERT_TRUE(RunPromise(online_bridge.consumer.promise()).is_ok());
  }

  for (uint32_t i = 0; i < kBufferCount; i++) {
    auto tx = right->AllocTx();
    ASSERT_TRUE(tx.is_valid());
    tx.data().SetFrameType(netdev::FrameType::ETHERNET);
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

  auto watcher1 = client->WatchStatus([&call_count1, &expect_online](netdev::Status status) {
    call_count1++;
    ASSERT_EQ(expect_online, static_cast<bool>(status.flags() & netdev::StatusFlags::ONLINE))
        << "Unexpected status flags " << static_cast<uint32_t>(status.flags())
        << ", online should be " << expect_online;
  });

  {
    auto watcher2 = client->WatchStatus([&call_count2](netdev::Status status) { call_count2++; });

    // Run loop with both watchers attached.
    ASSERT_TRUE(RunLoopUntilOrFailure([&call_count1, &call_count2]() {
      return call_count1 == 1 && call_count2 == 1;
    })) << "call_count1="
        << call_count1 << ", call_count2=" << call_count2;
    ASSERT_NO_FATAL_FAILURE();

    // Set online to false and wait for both watchers again.
    tun_device->SetOnline(false, []() {});
    expect_online = false;
    ASSERT_TRUE(RunLoopUntilOrFailure([&call_count1, &call_count2]() {
      return call_count1 == 2 && call_count2 == 2;
    })) << "call_count1="
        << call_count1 << ", call_count2=" << call_count2;
    ASSERT_NO_FATAL_FAILURE();
  }
  // watcher2 goes out of scope, Toggle online 3 times and expect that call_count2 will
  // not increase.
  for (uint32_t i = 0; i < 3; i++) {
    expect_online = !expect_online;
    tun_device->SetOnline(expect_online, []() {});
    ASSERT_TRUE(RunLoopUntilOrFailure([&call_count1, &i]() { return call_count1 == 3 + i; }))
        << "call_count1=" << call_count1 << ", call_count2=" << call_count2;
    ASSERT_NO_FATAL_FAILURE();
    // call_count2 mustn't change.
    ASSERT_EQ(call_count2, 2u);
  }
}

TEST_F(NetDeviceTest, ErrorCallback) {
  auto tun_device = OpenTunDevice();
  std::unique_ptr<NetworkDeviceClient> client;
  tun_device->ConnectProtocols(CreateClientRequest(&client));

  auto startup = RunPromise(StartSessionPromise(client.get())
                                .and_then([&client]() { ASSERT_OK(client->SetPaused(false)); })
                                .and_then(TapOnlinePromise(&tun_device)));
  ASSERT_NO_FATAL_FAILURE();
  ASSERT_TRUE(startup.is_ok()) << "Session startup failed: " << startup.error();
  ASSERT_TRUE(client->HasSession());

  // Test error callback gets called when the session is killed.
  zx_status_t error = ZX_OK;
  client->SetErrorCallback([&error](zx_status_t status) { error = status; });
  ASSERT_OK(client->KillSession());
  ASSERT_TRUE(RunLoopUntilOrFailure([&error]() { return error != ZX_OK; }));
  ASSERT_STATUS(error, ZX_ERR_CANCELED);
  ASSERT_FALSE(client->HasSession());

  // Test error callback gets called when the device disappears.
  error = ZX_OK;
  tun_device.Unbind().TakeChannel().reset();
  ASSERT_TRUE(RunLoopUntilOrFailure([&error]() { return error != ZX_OK; }));
  ASSERT_STATUS(error, ZX_ERR_PEER_CLOSED);
}

TEST_F(NetDeviceTest, PadTxFrames) {
  constexpr uint8_t kPayload[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A};
  constexpr size_t kMinBufferLength = sizeof(kPayload) - 2;
  constexpr size_t kSmallPayloadLength = kMinBufferLength - 2;
  auto device_config = DefaultDeviceConfig();
  device_config.mutable_base()->set_min_tx_buffer_length(kMinBufferLength);
  auto tun_device = OpenTunDevice(std::move(device_config));
  std::unique_ptr<NetworkDeviceClient> client;
  tun_device->ConnectProtocols(CreateClientRequest(&client));

  auto startup = RunPromise(StartSessionPromise(client.get())
                                .and_then([&client]() { ASSERT_OK(client->SetPaused(false)); })
                                .and_then(TapOnlinePromise(&tun_device)));
  ASSERT_NO_FATAL_FAILURE();
  ASSERT_TRUE(startup.is_ok()) << "Session startup failed: " << startup.error();
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
    tx.data().SetFrameType(netdev::FrameType::ETHERNET);
    EXPECT_EQ(tx.data().Write(frame.data(), frame.size()), frame.size());
    EXPECT_EQ(tx.Send(), ZX_OK);

    std::vector<uint8_t> expect(frame.begin(), frame.end());
    while (expect.size() < kMinBufferLength) {
      expect.push_back(0);
    }

    // Retrieve the frame and assert it's what we expect.
    bool done = false;
    tun_device->ReadFrame([&done, &expect](tun::Device_ReadFrame_Result result) {
      done = true;
      if (result.is_response()) {
        auto& frame = result.response().frame;
        ASSERT_EQ(frame.frame_type(), netdev::FrameType::ETHERNET);
        ASSERT_EQ(frame.data(), expect);
      } else {
        ADD_FAILURE() << "Read frame failed " << zx_status_get_string(result.err());
      }
    });
    ASSERT_TRUE(RunLoopUntilOrFailure([&done]() { return done; }));
  }
}

}  // namespace
