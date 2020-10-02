// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/sync/completion.h>
#include <lib/syslog/global.h>
#include <lib/zx/clock.h>
#include <zircon/device/network.h>
#include <zircon/status.h>

#include <fbl/span.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/lib/testing/predicates/status.h"
#include "tun_ctl.h"

namespace network {
namespace tun {
namespace testing {

namespace {
// Enable timeouts only to test things locally, committed code should not use timeouts.
constexpr zx::duration kTimeout = zx::duration::infinite();
}  // namespace

constexpr uint32_t kDefaultMtu = 1500;

// A very simple client to fuchsia.hardware.network.Device to run data path
// tests against.
class SimpleClient {
 public:
  static constexpr uint64_t kBufferSize = 2048;

  SimpleClient() = default;

  fuchsia::net::tun::Protocols NewRequest() {
    fuchsia::net::tun::Protocols protos;
    protos.set_network_device(device_.NewRequest());
    return protos;
  }

  zx_status_t OpenSession() {
    fuchsia::hardware::network::Info device_info;
    zx_status_t status = device_->GetInfo(&device_info);
    if (status != ZX_OK) {
      return status;
    }
    auto total_buffers = device_info.tx_depth + device_info.rx_depth;
    if ((status = data_.CreateAndMap(total_buffers * kBufferSize,
                                     ZX_VM_PERM_WRITE | ZX_VM_PERM_READ, nullptr, &data_vmo_)) !=
        ZX_OK) {
      return status;
    }
    if ((status = descriptors_.CreateAndMap(total_buffers * sizeof(buffer_descriptor_t),
                                            ZX_VM_PERM_WRITE | ZX_VM_PERM_READ, nullptr,
                                            &descriptors_vmo_)) != ZX_OK) {
      return status;
    }
    descriptor_count_ = total_buffers;
    rx_depth_ = device_info.rx_depth;
    tx_depth_ = device_info.tx_depth;
    fuchsia::hardware::network::SessionInfo session_info;
    session_info.options = fuchsia::hardware::network::SessionFlags::PRIMARY;
    session_info.descriptor_count = descriptor_count_;
    session_info.descriptor_version = NETWORK_DEVICE_DESCRIPTOR_VERSION;
    session_info.descriptor_length = sizeof(buffer_descriptor_t) / sizeof(uint64_t);
    session_info.rx_frames.push_back(fuchsia::hardware::network::FrameType::ETHERNET);

    if ((status = data_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &session_info.data)) != ZX_OK) {
      return status;
    }
    if ((status = descriptors_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &session_info.descriptors)) !=
        ZX_OK) {
      return status;
    }

    fuchsia::hardware::network::Device_OpenSession_Result open_session_result;
    status = device_->OpenSession("tun-test", std::move(session_info), &open_session_result);
    if (status != ZX_OK) {
      return status;
    }
    if (open_session_result.is_err()) {
      return open_session_result.err();
    }
    session_ = open_session_result.response().session.BindSync();
    rx_ = std::move(open_session_result.response().fifos.rx);
    tx_ = std::move(open_session_result.response().fifos.tx);
    return ZX_OK;
  }

  buffer_descriptor_t* descriptor(uint16_t index) {
    if (index > descriptor_count_) {
      return nullptr;
    }
    return static_cast<buffer_descriptor_t*>(descriptors_.start()) + index;
  }

  fbl::Span<uint8_t> data(const buffer_descriptor_t* desc) {
    return fbl::Span(static_cast<uint8_t*>(data_.start()) + desc->offset, desc->data_length);
  }

  void MintData(uint16_t didx, uint32_t len = 0) {
    auto* desc = descriptor(didx);
    if (len == 0) {
      len = desc->data_length;
    } else {
      desc->data_length = 4;
    }
    auto desc_data = data(desc);
    uint16_t i = 0;
    for (auto& b : desc_data) {
      b = static_cast<uint8_t>(i++ + didx);
    }
  }

  void ValidateDataInPlace(uint16_t desc, uint16_t mint_idx, uint32_t size = kBufferSize) {
    auto* d = descriptor(desc);
    ASSERT_EQ(d->data_length, size);
    auto desc_data = data(d).begin();

    for (uint32_t i = 0; i < size; i++) {
      ASSERT_EQ(*desc_data, static_cast<uint8_t>(i + mint_idx))
          << "Data mismatch at position " << i;
      desc_data++;
    }
  }

  static void ValidateData(const std::vector<uint8_t>& data, uint16_t didx) {
    ASSERT_EQ(data.size(), kBufferSize);
    for (uint32_t i = 0; i < data.size(); i++) {
      ASSERT_EQ(data[i], static_cast<uint8_t>(i + didx)) << "Data mismatch at position " << i;
    }
  }

  buffer_descriptor_t* ResetDescriptor(uint16_t index) {
    auto* desc = descriptor(index);
    *desc = {
        .frame_type = static_cast<uint8_t>(fuchsia::hardware::network::FrameType::ETHERNET),
        .chain_length = 0,
        .nxt = 0,
        .info_type = static_cast<uint32_t>(fuchsia::hardware::network::InfoType::NO_INFO),
        .offset = static_cast<uint64_t>(index) * kBufferSize,
        .head_length = 0,
        .tail_length = 0,
        .data_length = kBufferSize,
        .inbound_flags = 0,
        .return_flags = 0,
    };
    return desc;
  }

  zx_status_t SendDescriptors(zx::fifo* fifo, const std::vector<uint16_t>& descs, bool reset,
                              size_t count) {
    if (count == 0) {
      count = descs.size();
    }
    if (reset) {
      for (size_t i = 0; i < count; i++) {
        ResetDescriptor(descs[i]);
        MintData(descs[i]);
      }
    }
    return fifo->write(sizeof(uint16_t), &descs[0], count, nullptr);
  }

  zx_status_t SendTx(const std::vector<uint16_t>& descs, bool reset = false, size_t count = 0) {
    return SendDescriptors(&tx_, descs, reset, count);
  }

  zx_status_t SendRx(const std::vector<uint16_t>& descs, bool reset = true, size_t count = 0) {
    return SendDescriptors(&rx_, descs, reset, count);
  }

  zx_status_t FetchDescriptors(zx::fifo* fifo, uint16_t* out, size_t* count, bool wait) {
    size_t c = 1;
    if (!count) {
      count = &c;
    }
    if (wait) {
      auto status = fifo->wait_one(ZX_FIFO_READABLE, zx::deadline_after(kTimeout), nullptr);
      if (status != ZX_OK) {
        return status;
      }
    }
    return fifo->read(sizeof(uint16_t), out, *count, count);
  }

  zx_status_t FetchTx(uint16_t* out, size_t* count = nullptr, bool wait = true) {
    return FetchDescriptors(&tx_, out, count, wait);
  }

  zx_status_t FetchRx(uint16_t* out, size_t* count = nullptr, bool wait = true) {
    return FetchDescriptors(&rx_, out, count, wait);
  }

  zx_status_t WaitOnline() {
    fuchsia::hardware::network::StatusWatcherSyncPtr watcher;
    zx_status_t status = device_->GetStatusWatcher(watcher.NewRequest(), 5);
    if (status != ZX_OK) {
      return status;
    }
    bool online = false;
    while (!online) {
      fuchsia::hardware::network::Status device_status;
      status = watcher->WatchStatus(&device_status);
      if (status != ZX_OK) {
        return status;
      }
      online = static_cast<bool>(device_status.flags() &
                                 fuchsia::hardware::network::StatusFlags::ONLINE);
    }
    return ZX_OK;
  }

  fuchsia::hardware::network::SessionSyncPtr& session() { return session_; }

  fuchsia::hardware::network::DeviceSyncPtr& device() { return device_; }

  zx_status_t WaitRx() {
    return rx_.wait_one(ZX_FIFO_READABLE, zx::deadline_after(kTimeout), nullptr);
  }

  zx_status_t WaitTx() {
    return tx_.wait_one(ZX_FIFO_READABLE, zx::deadline_after(kTimeout), nullptr);
  }

  uint32_t rx_depth() const { return rx_depth_; }

  uint32_t tx_depth() const { return tx_depth_; }

 private:
  fuchsia::hardware::network::DeviceSyncPtr device_;
  zx::vmo data_vmo_;
  zx::vmo descriptors_vmo_;
  uint32_t descriptor_count_;
  fzl::VmoMapper data_;
  fzl::VmoMapper descriptors_;
  zx::fifo rx_;
  zx::fifo tx_;
  uint32_t tx_depth_;
  uint32_t rx_depth_;
  fuchsia::hardware::network::SessionSyncPtr session_;
};

class TunTest : public gtest::RealLoopFixture {
 public:
  TunTest()
      : gtest::RealLoopFixture(),
        loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        tun_ctl_(loop_.dispatcher()) {}

  void SetUp() override {
    fx_logger_config_t log_cfg = {
        .min_severity = -2,
        .console_fd = dup(STDOUT_FILENO),
        .log_service_channel = ZX_HANDLE_INVALID,
        .tags = nullptr,
        .num_tags = 0,
    };
    fx_log_reconfigure(&log_cfg);
    ASSERT_OK(loop_.StartThread("tun-test"));
  }

  void TearDown() override {
    // At the end of every test, all Device and DevicePair instances must be destroyed. We wait for
    // tun_ctl_ to observe all of them before destroying it and the async loop.
    sync_completion_t completion;
    tun_ctl_.SetSafeShutdownCallback([&completion]() { sync_completion_signal(&completion); });
    ASSERT_OK(sync_completion_wait(&completion, kTimeout.get()));
    // Loop must be shutdown before TunCtl. Shutdown the loop here so it's explicit and not reliant
    // on the order of the fields in the class.
    loop_.Shutdown();
  }

  fuchsia::net::tun::ControlSyncPtr Connect() {
    fuchsia::net::tun::ControlSyncPtr ret;
    tun_ctl_.Connect(ret.NewRequest());
    return ret;
  }

  static fuchsia::net::tun::BaseConfig DefaultBaseConfig() {
    fuchsia::net::tun::BaseConfig config;
    config.set_mtu(kDefaultMtu);
    config.set_rx_types({fuchsia::hardware::network::FrameType::ETHERNET});
    config.set_tx_types({fuchsia::hardware::network::FrameTypeSupport{
        fuchsia::hardware::network::FrameType::ETHERNET, 0,
        static_cast<fuchsia::hardware::network::TxFlags>(0)}});
    return config;
  }

  static fuchsia::net::tun::DeviceConfig DefaultDeviceConfig() {
    fuchsia::net::tun::DeviceConfig config;
    config.set_base(DefaultBaseConfig());
    config.set_mac(fuchsia::net::MacAddress{0x01, 0x02, 0x03, 0x04, 0x05, 0x06});
    config.set_blocking(true);
    return config;
  }

  static fuchsia::net::tun::DevicePairConfig DefaultDevicePairConfig() {
    fuchsia::net::tun::DevicePairConfig config;
    config.set_base(DefaultBaseConfig());
    config.set_mac_left(fuchsia::net::MacAddress{0x01, 0x02, 0x03, 0x04, 0x05, 0x06});
    config.set_mac_right(fuchsia::net::MacAddress{0x01, 0x02, 0x03, 0x04, 0x05, 0x07});
    return config;
  }

  fidl::InterfaceHandle<fuchsia::net::tun::Device> CreateDevice(
      fuchsia::net::tun::DeviceConfig config) {
    auto tun = Connect();
    fidl::InterfaceHandle<fuchsia::net::tun::Device> device;
    EXPECT_OK(tun->CreateDevice(std::move(config), device.NewRequest()));
    return device;
  }

  fidl::InterfaceHandle<fuchsia::net::tun::DevicePair> CreatePair(
      fuchsia::net::tun::DevicePairConfig config) {
    auto tun = Connect();
    fidl::InterfaceHandle<fuchsia::net::tun::DevicePair> device;
    EXPECT_OK(tun->CreatePair(std::move(config), device.NewRequest()));
    return device;
  }

 protected:
  async::Loop loop_;
  TunCtl tun_ctl_;
};

TEST_F(TunTest, InvalidConfigs) {
  auto wait_for_error = [this](fuchsia::net::tun::DeviceConfig config) -> zx_status_t {
    auto device = CreateDevice(std::move(config)).Bind();
    zx_status_t epitaph = ZX_OK;
    device.set_error_handler([&epitaph](zx_status_t err) { epitaph = err; });
    if (!RunLoopWithTimeoutOrUntil([&epitaph] { return epitaph != ZX_OK; }, kTimeout)) {
      return ZX_ERR_TIMED_OUT;
    }
    return epitaph;
  };
  // Zero MTU
  auto config = DefaultDeviceConfig();
  config.mutable_base()->set_mtu(0);
  ASSERT_STATUS(wait_for_error(std::move(config)), ZX_ERR_INVALID_ARGS);

  // MTU too large
  config = DefaultDeviceConfig();
  config.mutable_base()->set_mtu(fuchsia::net::tun::MAX_MTU + 1);
  ASSERT_STATUS(wait_for_error(std::move(config)), ZX_ERR_INVALID_ARGS);

  // No Rx frames
  config = DefaultDeviceConfig();
  config.mutable_base()->clear_rx_types();
  ASSERT_STATUS(wait_for_error(std::move(config)), ZX_ERR_INVALID_ARGS);

  // No Tx frames
  config = DefaultDeviceConfig();
  config.mutable_base()->clear_tx_types();
  ASSERT_STATUS(wait_for_error(std::move(config)), ZX_ERR_INVALID_ARGS);

  // Empty Rx frames
  config = DefaultDeviceConfig();
  config.mutable_base()->set_rx_types({});
  ASSERT_STATUS(wait_for_error(std::move(config)), ZX_ERR_INVALID_ARGS);

  // Empty Tx frames
  config = DefaultDeviceConfig();
  config.mutable_base()->set_tx_types({});
  ASSERT_STATUS(wait_for_error(std::move(config)), ZX_ERR_INVALID_ARGS);
}

TEST_F(TunTest, ConnectNetworkDevice) {
  auto tun = CreateDevice(DefaultDeviceConfig()).BindSync();
  fuchsia::hardware::network::DeviceSyncPtr device;
  fuchsia::net::tun::Protocols protos;
  protos.set_network_device(device.NewRequest());
  ASSERT_OK(tun->ConnectProtocols(std::move(protos)));
  fuchsia::hardware::network::Info info;
  ASSERT_OK(device->GetInfo(&info));
}

TEST_F(TunTest, Teardown) {
  auto tun = CreateDevice(DefaultDeviceConfig()).BindSync();
  fuchsia::hardware::network::DevicePtr device;
  fuchsia::hardware::network::MacAddressingPtr mac;
  fuchsia::net::tun::Protocols protos;
  protos.set_mac_addressing(mac.NewRequest());
  protos.set_network_device(device.NewRequest());
  ASSERT_OK(tun->ConnectProtocols(std::move(protos)));
  bool device_dead = false;
  bool mac_dead = false;
  device.set_error_handler([&device_dead](zx_status_t status) {
    EXPECT_STATUS(status, ZX_ERR_PEER_CLOSED);
    device_dead = true;
  });
  mac.set_error_handler([&mac_dead](zx_status_t status) {
    EXPECT_STATUS(status, ZX_ERR_PEER_CLOSED);
    mac_dead = true;
  });
  // get rid of tun.
  tun.Unbind().TakeChannel().reset();
  ASSERT_TRUE(
      RunLoopWithTimeoutOrUntil([&device_dead, &mac_dead]() { return device_dead && mac_dead; },
                                kTimeout, zx::duration::infinite()))
      << "Timed out waiting for channels to close; device_dead=" << device_dead
      << ", mac_dead=" << mac_dead;
}

TEST_F(TunTest, Status) {
  auto tun = CreateDevice(DefaultDeviceConfig()).BindSync();
  fuchsia::hardware::network::DeviceSyncPtr device;
  fuchsia::net::tun::Protocols protos;
  protos.set_network_device(device.NewRequest());
  ASSERT_OK(tun->ConnectProtocols(std::move(protos)));
  fuchsia::hardware::network::Status device_status;
  ASSERT_OK(device->GetStatus(&device_status));
  ASSERT_EQ(device_status.mtu(), kDefaultMtu);
  ASSERT_EQ(device_status.flags(), fuchsia::hardware::network::StatusFlags());
  fuchsia::hardware::network::StatusWatcherSyncPtr watcher;

  ASSERT_OK(device->GetStatusWatcher(watcher.NewRequest(), 5));

  ASSERT_OK(watcher->WatchStatus(&device_status));
  ASSERT_EQ(device_status.mtu(), kDefaultMtu);
  ASSERT_EQ(device_status.flags(), fuchsia::hardware::network::StatusFlags());

  ASSERT_OK(tun->SetOnline(true));

  ASSERT_OK(watcher->WatchStatus(&device_status));
  ASSERT_EQ(device_status.mtu(), kDefaultMtu);
  ASSERT_EQ(device_status.flags(), fuchsia::hardware::network::StatusFlags::ONLINE);
}

TEST_F(TunTest, Mac) {
  auto config = DefaultDeviceConfig();
  fuchsia::net::MacAddress ref_mac;
  config.mac().Clone(&ref_mac);
  auto tun = CreateDevice(std::move(config)).BindSync();
  fuchsia::hardware::network::MacAddressingSyncPtr mac;
  fuchsia::net::tun::Protocols protos;
  protos.set_mac_addressing(mac.NewRequest());
  ASSERT_OK(tun->ConnectProtocols(std::move(protos)));

  fuchsia::net::MacAddress unicast;

  ASSERT_OK(mac->GetUnicastAddress(&unicast));
  fuchsia::net::tun::InternalState internal_state;

  ASSERT_OK(tun->WatchState(&internal_state));
  ASSERT_TRUE(internal_state.has_mac());
  ASSERT_EQ(internal_state.mac().mode(),
            fuchsia::hardware::network::MacFilterMode::MULTICAST_FILTER);

  zx_status_t status;
  ASSERT_OK(mac->AddMulticastAddress(fuchsia::net::MacAddress{1, 10, 20, 30, 40, 50}, &status));
  ASSERT_OK(status);

  ASSERT_OK(tun->WatchState(&internal_state));
  ASSERT_EQ(internal_state.mac().mode(),
            fuchsia::hardware::network::MacFilterMode::MULTICAST_FILTER);
  ASSERT_EQ(internal_state.mac().multicast_filters().size(), 1ul);
  std::array<uint8_t, 6> cmp{1, 10, 20, 30, 40, 50};
  ASSERT_EQ(internal_state.mac().multicast_filters()[0].octets, cmp);

  ASSERT_OK(mac->SetMode(fuchsia::hardware::network::MacFilterMode::PROMISCUOUS, &status));
  ASSERT_OK(status);
  ASSERT_OK(tun->WatchState(&internal_state));
  ASSERT_EQ(internal_state.mac().mode(), fuchsia::hardware::network::MacFilterMode::PROMISCUOUS);
  ASSERT_TRUE(internal_state.mac().multicast_filters().empty())
      << "Mac filters should be empty, but has " << internal_state.mac().multicast_filters().size()
      << " entries";
}

TEST_F(TunTest, NoMac) {
  auto config = DefaultDeviceConfig();
  // remove mac information
  config.clear_mac();
  auto tun = CreateDevice(std::move(config)).BindSync();
  fuchsia::hardware::network::MacAddressingPtr mac;
  fuchsia::net::tun::Protocols protos;
  protos.set_mac_addressing(mac.NewRequest());
  ASSERT_OK(tun->ConnectProtocols(std::move(protos)));
  bool done = false;

  // mac should be closed because we created tun without a mac information.
  // Wait for the error handler to report that back to us.
  mac.set_error_handler([&done](zx_status_t error) {
    EXPECT_STATUS(error, ZX_ERR_PEER_CLOSED);
    done = true;
  });
  ASSERT_TRUE(
      RunLoopWithTimeoutOrUntil([&done]() { return done; }, kTimeout, zx::duration::infinite()));

  fuchsia::net::tun::InternalState internal_state;
  ASSERT_OK(tun->GetState(&internal_state));
  ASSERT_FALSE(internal_state.has_mac()) << "Mac state should be unset";
}

TEST_F(TunTest, SimpleRxTx) {
  auto config = DefaultDeviceConfig();
  config.set_online(true);
  config.set_blocking(false);
  auto tun = CreateDevice(std::move(config)).BindSync();

  SimpleClient client;
  ASSERT_OK(tun->ConnectProtocols(client.NewRequest()));
  ASSERT_OK(client.OpenSession());

  ASSERT_OK(client.session()->SetPaused(false));

  zx::eventpair signals;
  ASSERT_OK(tun->GetSignals(&signals));

  // Attempting to read frame without any available buffers should fail with should_wait and the
  // readable signal should not be set.
  fuchsia::net::tun::Device_ReadFrame_Result read_frame_result;
  ASSERT_OK(tun->ReadFrame(&read_frame_result));
  ASSERT_TRUE(read_frame_result.is_err())
      << "Got unexpected frame with " << read_frame_result.response().frame.data().size()
      << "bytes, should've errored";
  ASSERT_STATUS(read_frame_result.err(), ZX_ERR_SHOULD_WAIT);
  ASSERT_STATUS(signals.wait_one(static_cast<uint32_t>(fuchsia::net::tun::Signals::READABLE),
                                 zx::time::infinite_past(), nullptr),
                ZX_ERR_TIMED_OUT);

  ASSERT_OK(client.SendTx({0x00, 0x01}, true));
  ASSERT_OK(signals.wait_one(static_cast<uint32_t>(fuchsia::net::tun::Signals::READABLE),
                             zx::deadline_after(kTimeout), nullptr));

  ASSERT_OK(tun->ReadFrame(&read_frame_result));
  ASSERT_TRUE(read_frame_result.is_response())
      << "ReadFrame failed: " << zx_status_get_string(read_frame_result.err());
  ASSERT_EQ(read_frame_result.response().frame.frame_type(),
            fuchsia::hardware::network::FrameType::ETHERNET);
  ASSERT_NO_FATAL_FAILURE(
      SimpleClient::ValidateData(read_frame_result.response().frame.data(), 0x00));
  ASSERT_FALSE(read_frame_result.response().frame.has_meta());

  // After read frame, the first descriptor must've been returned.
  uint16_t desc;
  ASSERT_OK(client.FetchTx(&desc));
  EXPECT_EQ(desc, 0x00);

  // Attempting to send a frame without any available buffers should fail with should_wait and the
  // writable signal should not be set.
  fuchsia::net::tun::Device_WriteFrame_Result write_frame_result;
  {
    fuchsia::net::tun::Frame frame;
    frame.set_frame_type(fuchsia::hardware::network::FrameType::ETHERNET);
    frame.set_data({0xAA, 0xBB});
    ASSERT_OK(tun->WriteFrame(std::move(frame), &write_frame_result));
    ASSERT_TRUE(write_frame_result.is_err());
    ASSERT_STATUS(write_frame_result.err(), ZX_ERR_SHOULD_WAIT);
    ASSERT_STATUS(signals.wait_one(static_cast<uint32_t>(fuchsia::net::tun::Signals::WRITABLE),
                                   zx::time::infinite_past(), nullptr),
                  ZX_ERR_TIMED_OUT);
  }

  ASSERT_OK(client.SendRx({0x02}, true));

  // But if we sent stuff out, now it should work after waiting for the available signal.
  ASSERT_OK(signals.wait_one(static_cast<uint32_t>(fuchsia::net::tun::Signals::WRITABLE),
                             zx::deadline_after(kTimeout), nullptr));
  {
    fuchsia::net::tun::Frame frame;
    frame.set_frame_type(fuchsia::hardware::network::FrameType::ETHERNET);
    frame.set_data({0xAA, 0xBB});
    ASSERT_OK(tun->WriteFrame(std::move(frame), &write_frame_result));
    ASSERT_TRUE(write_frame_result.is_response())
        << "Write frame failed with " << zx_status_get_string(write_frame_result.err());
  }
  // Check that data was correctly written to descriptor.
  ASSERT_OK(client.FetchRx(&desc));
  ASSERT_EQ(desc, 0x02);
  auto* d = client.descriptor(desc);
  EXPECT_EQ(d->data_length, 2u);
  auto data = client.data(d);
  EXPECT_EQ(data[0], 0xAA);
  EXPECT_EQ(data[1], 0xBB);
}

TEST_F(TunTest, PairRxTx) {
  auto dpair = CreatePair(DefaultDevicePairConfig()).BindSync();
  fuchsia::net::tun::DevicePairEnds ends;
  SimpleClient left;
  SimpleClient right;
  ends.set_left(left.NewRequest());
  ends.set_right(right.NewRequest());
  ASSERT_OK(dpair->ConnectProtocols(std::move(ends)));

  ASSERT_OK(left.OpenSession());
  ASSERT_OK(right.OpenSession());

  ASSERT_OK(right.SendRx({0x05, 0x06, 0x07}, true));
  right.session()->SetPaused(false);
  left.session()->SetPaused(false);
  ASSERT_OK(left.WaitOnline());
  ASSERT_OK(right.WaitOnline());

  ASSERT_OK(left.SendTx({0x00, 0x01, 0x02}, true));
  ASSERT_OK(right.WaitRx());
  uint16_t ds[3];
  size_t dcount = 3;
  ASSERT_OK(right.FetchRx(ds, &dcount));
  ASSERT_EQ(dcount, 3u);
  // Check that the data was copied correctly for all three descriptors.
  uint16_t ref_d = 0x00;
  for (const auto& d : ds) {
    ASSERT_NO_FATAL_FAILURE(right.ValidateDataInPlace(d, ref_d))
        << "Invalid in place data for " << d << " <-> " << ref_d;
    ref_d++;
  }

  ASSERT_OK(left.WaitTx());
  ASSERT_OK(left.FetchTx(ds, &dcount));
  EXPECT_EQ(dcount, 3u);
}

TEST_F(TunTest, PairOnlineSignal) {
  auto dpair = CreatePair(DefaultDevicePairConfig()).BindSync();
  fuchsia::net::tun::DevicePairEnds ends;
  SimpleClient left;
  SimpleClient right;
  ends.set_left(left.NewRequest());
  ends.set_right(right.NewRequest());
  ASSERT_OK(dpair->ConnectProtocols(std::move(ends)));

  ASSERT_OK(left.OpenSession());
  ASSERT_OK(right.OpenSession());

  // Online status should be false for both sides before a session is opened.
  fuchsia::hardware::network::StatusWatcherSyncPtr wleft, wright;
  left.device()->GetStatusWatcher(wleft.NewRequest(), 2);
  right.device()->GetStatusWatcher(wright.NewRequest(), 2);

  fuchsia::hardware::network::Status sleft, sright;
  ASSERT_OK(wleft->WatchStatus(&sleft));
  ASSERT_OK(wright->WatchStatus(&sright));

  EXPECT_EQ(sleft.flags() & fuchsia::hardware::network::StatusFlags::ONLINE,
            fuchsia::hardware::network::StatusFlags());
  EXPECT_EQ(sright.flags() & fuchsia::hardware::network::StatusFlags::ONLINE,
            fuchsia::hardware::network::StatusFlags());

  // When both sessions are unpaused, online signal must come up.
  ASSERT_OK(left.session()->SetPaused(false));
  ASSERT_OK(right.session()->SetPaused(false));

  ASSERT_OK(wleft->WatchStatus(&sleft));
  ASSERT_OK(wright->WatchStatus(&sright));

  EXPECT_EQ(sleft.flags() & fuchsia::hardware::network::StatusFlags::ONLINE,
            fuchsia::hardware::network::StatusFlags::ONLINE);
  EXPECT_EQ(sright.flags() & fuchsia::hardware::network::StatusFlags::ONLINE,
            fuchsia::hardware::network::StatusFlags::ONLINE);
}

TEST_F(TunTest, PairFallibleWrites) {
  auto config = DefaultDevicePairConfig();
  config.set_fallible_transmit_left(true);

  auto dpair = CreatePair(std::move(config)).BindSync();
  fuchsia::net::tun::DevicePairEnds ends;
  SimpleClient left;
  SimpleClient right;
  ends.set_left(left.NewRequest());
  ends.set_right(right.NewRequest());
  ASSERT_OK(dpair->ConnectProtocols(std::move(ends)));

  ASSERT_OK(left.OpenSession());
  ASSERT_OK(right.OpenSession());
  ASSERT_OK(left.session()->SetPaused(false));
  ASSERT_OK(right.session()->SetPaused(false));
  ASSERT_OK(left.WaitOnline());
  ASSERT_OK(right.WaitOnline());

  ASSERT_OK(left.SendTx({0x00}, true));
  ASSERT_OK(left.WaitTx());
  uint16_t desc;
  ASSERT_OK(left.FetchTx(&desc));
  ASSERT_EQ(desc, 0x00);
  auto* d = left.descriptor(desc);
  auto flags = static_cast<fuchsia::hardware::network::TxReturnFlags>(d->return_flags);
  EXPECT_EQ(flags & fuchsia::hardware::network::TxReturnFlags::TX_RET_ERROR,
            fuchsia::hardware::network::TxReturnFlags::TX_RET_ERROR);
}

TEST_F(TunTest, PairInfallibleWrites) {
  auto dpair = CreatePair(DefaultDevicePairConfig()).BindSync();
  fuchsia::net::tun::DevicePairEnds ends;
  SimpleClient left;
  SimpleClient right;
  ends.set_left(left.NewRequest());
  ends.set_right(right.NewRequest());
  ASSERT_OK(dpair->ConnectProtocols(std::move(ends)));

  ASSERT_OK(left.OpenSession());
  ASSERT_OK(right.OpenSession());
  ASSERT_OK(left.session()->SetPaused(false));
  ASSERT_OK(right.session()->SetPaused(false));
  ASSERT_OK(left.WaitOnline());
  ASSERT_OK(right.WaitOnline());

  ASSERT_OK(left.SendTx({0x00}, true));

  uint16_t desc;
  ASSERT_STATUS(left.FetchTx(&desc, nullptr, false), ZX_ERR_SHOULD_WAIT);

  ASSERT_OK(right.SendRx({0x01}, true));
  ASSERT_OK(right.WaitRx());
  ASSERT_OK(left.WaitTx());
  ASSERT_OK(left.FetchTx(&desc));
  EXPECT_EQ(desc, 0x00);
  ASSERT_OK(right.FetchRx(&desc));
  EXPECT_EQ(desc, 0x01);
}

TEST_F(TunTest, RejectsIfOffline) {
  auto tun = CreateDevice(DefaultDeviceConfig()).BindSync();
  SimpleClient cli;
  ASSERT_OK(tun->ConnectProtocols(cli.NewRequest()));
  ASSERT_OK(cli.OpenSession());
  ASSERT_OK(cli.session()->SetPaused(false));

  // Can't send from the tun end.
  {
    fuchsia::net::tun::Frame frame;
    frame.set_frame_type(fuchsia::hardware::network::FrameType::ETHERNET);
    frame.set_data({0x01, 0x02, 0x03});
    fuchsia::net::tun::Device_WriteFrame_Result result;
    ASSERT_OK(tun->WriteFrame(std::move(frame), &result));
    ASSERT_TRUE(result.is_err());
    ASSERT_STATUS(result.err(), ZX_ERR_BAD_STATE);
  }
  // Can't send from client end.
  {
    ASSERT_OK(cli.SendTx({0x00}, true));
    ASSERT_OK(cli.WaitTx());
    uint16_t desc;
    ASSERT_OK(cli.FetchTx(&desc));
    ASSERT_EQ(desc, 0x00);
    ASSERT_TRUE(static_cast<bool>(
        fuchsia::hardware::network::TxReturnFlags(cli.descriptor(desc)->return_flags) &
        fuchsia::hardware::network::TxReturnFlags::TX_RET_ERROR))
        << "Bad return flags " << cli.descriptor(desc)->return_flags;
  }
  // If we set online we'll be able to send.
  ASSERT_OK(tun->SetOnline(true));

  // Send from client end once more and read a single frame.
  {
    ASSERT_OK(cli.SendTx({0x00, 0x01}, true));
    fuchsia::net::tun::Device_ReadFrame_Result result;
    ASSERT_OK(tun->ReadFrame(&result));
    ASSERT_TRUE(result.is_response())
        << "ReadFrame failed with " << zx_status_get_string(result.err());
  }
  // Set offline and see if client received their tx buffers back.
  ASSERT_OK(tun->SetOnline(false));

  uint16_t desc;
  ASSERT_OK(cli.WaitTx());
  // No error on first descriptor.
  ASSERT_OK(cli.FetchTx(&desc));
  ASSERT_EQ(desc, 0x00);
  EXPECT_FALSE(static_cast<bool>(
      fuchsia::hardware::network::TxReturnFlags(cli.descriptor(desc)->return_flags) &
      fuchsia::hardware::network::TxReturnFlags::TX_RET_ERROR))
      << "Bad return flags " << cli.descriptor(desc)->return_flags;
  // Error on second.
  ASSERT_OK(cli.FetchTx(&desc));
  ASSERT_EQ(desc, 0x01);
  EXPECT_TRUE(static_cast<bool>(
      fuchsia::hardware::network::TxReturnFlags(cli.descriptor(desc)->return_flags) &
      fuchsia::hardware::network::TxReturnFlags::TX_RET_ERROR))
      << "Bad return flags " << cli.descriptor(desc)->return_flags;
}

TEST_F(TunTest, PairEcho) {
  auto dpair = CreatePair(DefaultDevicePairConfig()).BindSync();
  fuchsia::net::tun::DevicePairEnds ends;
  SimpleClient left;
  SimpleClient right;
  ends.set_left(left.NewRequest());
  ends.set_right(right.NewRequest());
  ASSERT_OK(dpair->ConnectProtocols(std::move(ends)));

  ASSERT_OK(left.OpenSession());
  ASSERT_OK(right.OpenSession());
  ASSERT_OK(left.session()->SetPaused(false));
  ASSERT_OK(right.session()->SetPaused(false));
  ASSERT_OK(left.WaitOnline());
  ASSERT_OK(right.WaitOnline());

  auto rx_depth = left.rx_depth();
  auto tx_depth = left.tx_depth();

  std::vector<uint16_t> tx_descriptors;
  std::vector<uint16_t> rx_descriptors;
  tx_descriptors.reserve(tx_depth);
  rx_descriptors.reserve(rx_depth);
  for (uint16_t i = 0; i < static_cast<uint16_t>(tx_depth); i++) {
    tx_descriptors.push_back(i);
    left.ResetDescriptor(i);
    left.MintData(i, 4);
    right.ResetDescriptor(i);
  }
  for (uint16_t i = tx_depth; i < static_cast<uint16_t>(tx_depth + rx_depth); i++) {
    rx_descriptors.push_back(i);
    left.ResetDescriptor(i);
    right.ResetDescriptor(i);
  }
  ASSERT_OK(right.SendRx(rx_descriptors));
  ASSERT_OK(left.SendRx(rx_descriptors));

  ASSERT_OK(left.SendTx(tx_descriptors));

  uint32_t echoed = 0;

  std::vector<uint16_t> rx_buffer(rx_depth, 0);
  std::vector<uint16_t> tx_buffer(tx_depth, 0);

  while (echoed < tx_depth) {
    ASSERT_OK(right.WaitRx());
    size_t count = rx_depth;
    ASSERT_OK(right.FetchRx(&rx_buffer[0], &count));
    for (size_t i = 0; i < count; i++) {
      tx_buffer[i] = tx_descriptors[i + echoed];
      auto* rx_desc = right.descriptor(rx_buffer[i]);
      auto* tx_desc = right.descriptor(tx_buffer[i]);
      auto rx_data = right.data(rx_desc);
      auto tx_data = right.data(tx_desc);
      std::copy(rx_data.begin(), rx_data.end(), tx_data.begin());
      tx_desc->frame_type = rx_desc->frame_type;
      tx_desc->data_length = rx_desc->data_length;
      rx_desc->data_length = SimpleClient::kBufferSize;
    }
    echoed += count;
    ASSERT_OK(right.SendTx(tx_buffer, false, count));
    ASSERT_OK(right.SendRx(rx_buffer, false, count));
  }

  uint32_t received = 0;
  while (received < tx_depth) {
    ASSERT_OK(left.WaitRx());
    size_t count = rx_depth;
    ASSERT_OK(left.FetchRx(&rx_buffer[0], &count));
    for (size_t i = 0; i < count; i++) {
      auto orig_desc = tx_descriptors[received + i];
      ASSERT_NO_FATAL_FAILURE(left.ValidateDataInPlace(rx_buffer[i], orig_desc, 4));
    }
    received += count;
  }
}

}  // namespace testing
}  // namespace tun
}  // namespace network
