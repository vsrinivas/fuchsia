// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/sync/completion.h>
#include <lib/syslog/global.h>
#include <lib/zx/time.h>
#include <zircon/device/network.h>
#include <zircon/status.h>

#include <fbl/span.h>
#include <gmock/gmock.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/lib/testing/predicates/status.h"
#include "tun_ctl.h"

namespace network {
namespace tun {
namespace testing {

namespace {
// Enable timeouts only to test things locally, committed code should not use timeouts.
constexpr zx::duration kTimeout = zx::duration::infinite();
// TODO(http://fxbug.dev/64310): Do not assume port 0 once tun FIDL supports ports.
constexpr uint8_t kPort0 = 0;

zx::status<fidl::ClientEnd<fuchsia_hardware_network::StatusWatcher>> GetStatusWatcher(
    fidl::ClientEnd<fuchsia_hardware_network::Device>& device, uint8_t port, uint32_t buffer) {
  zx::status port_endpoints = fidl::CreateEndpoints<fuchsia_hardware_network::Port>();
  if (port_endpoints.is_error()) {
    return port_endpoints.take_error();
  }
  {
    fidl::WireResult result =
        fidl::WireCall(device).GetPort(kPort0, std::move(port_endpoints->server));
    if (!result.ok()) {
      return zx::error(result.status());
    }
  }

  zx::status watcher_endpoints = fidl::CreateEndpoints<fuchsia_hardware_network::StatusWatcher>();
  if (watcher_endpoints.is_error()) {
    return watcher_endpoints.take_error();
  }

  {
    fidl::WireResult result = fidl::WireCall(port_endpoints->client)
                                  .GetStatusWatcher(std::move(watcher_endpoints->server), buffer);
    if (!result.ok()) {
      return zx::error(result.status());
    }
  }

  return zx::ok(std::move(watcher_endpoints->client));
}

}  // namespace

constexpr uint32_t kDefaultMtu = 1500;

// A very simple client to fuchsia.hardware.network.Device to run data path
// tests against.
class SimpleClient {
 public:
  static constexpr uint64_t kBufferSize = 2048;

  SimpleClient() = default;

  zx::status<fuchsia_net_tun::wire::Protocols> NewRequest() {
    zx::status server_end = fidl::CreateEndpoints(&device_.client_end());
    if (server_end.is_error()) {
      return server_end.take_error();
    }
    fuchsia_net_tun::wire::Protocols protos(alloc_);
    protos.set_network_device(alloc_, std::move(server_end.value()));
    return zx::ok(protos);
  }

  zx_status_t OpenSession() {
    fidl::WireResult info_result = device().GetInfo();
    if (!info_result.ok()) {
      return info_result.status();
    }
    fuchsia_hardware_network::wire::DeviceInfo& device_info = info_result.value().info;
    if (!(device_info.has_tx_depth() && device_info.has_rx_depth())) {
      return ZX_ERR_INTERNAL;
    }
    const uint16_t tx_depth = device_info.tx_depth();
    const uint16_t rx_depth = device_info.rx_depth();
    const uint16_t total_buffers = tx_depth + rx_depth;
    zx_status_t status;
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
    rx_depth_ = rx_depth;
    tx_depth_ = tx_depth;
    fuchsia_hardware_network::wire::SessionInfo session_info(alloc_);
    session_info.set_descriptor_version(alloc_, NETWORK_DEVICE_DESCRIPTOR_VERSION);
    session_info.set_descriptor_length(alloc_, sizeof(buffer_descriptor_t) / sizeof(uint64_t));
    session_info.set_descriptor_count(alloc_, descriptor_count_);
    session_info.set_options(alloc_, fuchsia_hardware_network::wire::SessionFlags::kPrimary);

    zx::vmo data;
    if ((status = data_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &data)) != ZX_OK) {
      return status;
    }
    session_info.set_data(alloc_, std::move(data));

    zx::vmo descriptors;
    if ((status = descriptors_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &descriptors)) != ZX_OK) {
      return status;
    }
    session_info.set_descriptors(alloc_, std::move(descriptors));

    fidl::WireResult session_result = device().OpenSession("tun-test", std::move(session_info));
    if (!session_result.ok()) {
      return session_result.status();
    }
    fuchsia_hardware_network::wire::DeviceOpenSessionResult& result = session_result.value().result;
    switch (result.which()) {
      case fuchsia_hardware_network::wire::DeviceOpenSessionResult::Tag::kErr:
        return result.err();
      case fuchsia_hardware_network::wire::DeviceOpenSessionResult::Tag::kResponse:
        fuchsia_hardware_network::wire::DeviceOpenSessionResponse& response =
            result.mutable_response();
        session_ = fidl::BindSyncClient(std::move(response.session));
        rx_ = std::move(response.fifos.rx);
        tx_ = std::move(response.fifos.tx);
        return ZX_OK;
    }
  }

  zx_status_t AttachPort(uint8_t port_id,
                         std::vector<fuchsia_hardware_network::wire::FrameType> frames = {
                             fuchsia_hardware_network::wire::FrameType::kEthernet}) {
    fidl::WireResult wire_result = session_.Attach(
        port_id, fidl::VectorView<fuchsia_hardware_network::wire::FrameType>::FromExternal(frames));
    if (!wire_result.ok()) {
      return wire_result.status();
    }
    const auto& result = wire_result.value().result;
    switch (result.which()) {
      case fuchsia_hardware_network::wire::SessionAttachResult::Tag::kResponse:
        return ZX_OK;
      case fuchsia_hardware_network::wire::SessionAttachResult::Tag::kErr:
        return result.err();
    }
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

  static void ValidateData(const fidl::VectorView<uint8_t>& data, uint16_t didx) {
    ASSERT_EQ(data.count(), kBufferSize);
    for (uint32_t i = 0; i < data.count(); i++) {
      ASSERT_EQ(data[i], static_cast<uint8_t>(i + didx)) << "Data mismatch at position " << i;
    }
  }

  buffer_descriptor_t* ResetDescriptor(uint16_t index) {
    auto* desc = descriptor(index);
    *desc = {
        .frame_type = static_cast<uint8_t>(fuchsia_hardware_network::wire::FrameType::kEthernet),
        .chain_length = 0,
        .nxt = 0,
        .info_type = static_cast<uint32_t>(fuchsia_hardware_network::wire::InfoType::kNoInfo),
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

  zx_status_t FetchDescriptors(zx::fifo& fifo, uint16_t* out, size_t* count, bool wait) {
    size_t c = 1;
    if (!count) {
      count = &c;
    }
    if (wait) {
      auto status = fifo.wait_one(ZX_FIFO_READABLE, zx::deadline_after(kTimeout), nullptr);
      if (status != ZX_OK) {
        return status;
      }
    }
    return fifo.read(sizeof(uint16_t), out, *count, count);
  }

  zx_status_t FetchTx(uint16_t* out, size_t* count = nullptr, bool wait = true) {
    return FetchDescriptors(tx_, out, count, wait);
  }

  zx_status_t FetchRx(uint16_t* out, size_t* count = nullptr, bool wait = true) {
    return FetchDescriptors(rx_, out, count, wait);
  }

  zx_status_t WaitOnline() {
    zx::status watcher = GetStatusWatcher(device().client_end(), kPort0, 5);
    if (watcher.is_error()) {
      return watcher.error_value();
    }
    bool online = false;
    while (!online) {
      fidl::WireResult result = fidl::WireCall(watcher.value()).WatchStatus();
      if (!result.ok()) {
        return result.status();
      }
      fuchsia_hardware_network::wire::PortStatus status = result.value().port_status;
      online = status.has_flags() &&
               status.flags() & fuchsia_hardware_network::wire::StatusFlags::kOnline;
    }
    return ZX_OK;
  }

  fidl::WireSyncClient<fuchsia_hardware_network::Session>& session() { return session_; }

  fidl::WireSyncClient<fuchsia_hardware_network::Device>& device() { return device_; }

  zx_status_t WaitRx() {
    return rx_.wait_one(ZX_FIFO_READABLE, zx::deadline_after(kTimeout), nullptr);
  }

  zx_status_t WaitTx() {
    return tx_.wait_one(ZX_FIFO_READABLE, zx::deadline_after(kTimeout), nullptr);
  }

  uint16_t rx_depth() const { return rx_depth_; }

  uint16_t tx_depth() const { return tx_depth_; }

 private:
  fidl::FidlAllocator<> alloc_;

  fidl::WireSyncClient<fuchsia_hardware_network::Device> device_;
  zx::vmo data_vmo_;
  zx::vmo descriptors_vmo_;
  uint16_t descriptor_count_;
  fzl::VmoMapper data_;
  fzl::VmoMapper descriptors_;
  zx::fifo rx_;
  zx::fifo tx_;
  uint16_t tx_depth_;
  uint16_t rx_depth_;
  fidl::WireSyncClient<fuchsia_hardware_network::Session> session_;
};

class TunTest : public gtest::RealLoopFixture {
 protected:
  TunTest()
      : gtest::RealLoopFixture(),
        tun_ctl_loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        tun_ctl_(tun_ctl_loop_.dispatcher()) {}

  void SetUp() override {
    fx_logger_config_t log_cfg = {
        .min_severity = -2,
        .console_fd = dup(STDOUT_FILENO),
        .log_service_channel = ZX_HANDLE_INVALID,
        .tags = nullptr,
        .num_tags = 0,
    };
    fx_log_reconfigure(&log_cfg);
    ASSERT_OK(tun_ctl_loop_.StartThread("tun-test"));
  }

  void TearDown() override {
    // At the end of every test, all Device and DevicePair instances must be destroyed. We wait for
    // tun_ctl_ to observe all of them before destroying it and the async loop.
    sync_completion_t completion;
    tun_ctl_.SetSafeShutdownCallback([&completion]() { sync_completion_signal(&completion); });
    ASSERT_OK(sync_completion_wait(&completion, kTimeout.get()));
    // Loop must be shutdown before TunCtl. Shutdown the loop here so it's explicit and not reliant
    // on the order of the fields in the class.
    tun_ctl_loop_.Shutdown();
  }

  zx::status<fidl::WireSyncClient<fuchsia_net_tun::Control>> Connect() {
    zx::status endpoints = fidl::CreateEndpoints<fuchsia_net_tun::Control>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }
    tun_ctl_.Connect(std::move(endpoints->server));
    return zx::ok(fidl::BindSyncClient(std::move(endpoints->client)));
  }

  fuchsia_net_tun::wire::BaseConfig DefaultBaseConfig() {
    fuchsia_net_tun::wire::BaseConfig config(alloc_);
    config.set_mtu(alloc_, kDefaultMtu);
    const fuchsia_hardware_network::wire::FrameType rx_types[] = {
        fuchsia_hardware_network::wire::FrameType::kEthernet,
    };
    fidl::VectorView<fuchsia_hardware_network::wire::FrameType> rx_types_view(alloc_,
                                                                              std::size(rx_types));
    std::copy(std::begin(rx_types), std::end(rx_types), rx_types_view.mutable_data());
    const fuchsia_hardware_network::wire::FrameTypeSupport tx_types[] = {
        fuchsia_hardware_network::wire::FrameTypeSupport{
            .type = fuchsia_hardware_network::wire::FrameType::kEthernet,
        },
    };
    fidl::VectorView<fuchsia_hardware_network::wire::FrameTypeSupport> tx_types_view(
        alloc_, std::size(tx_types));
    std::copy(std::begin(tx_types), std::end(tx_types), tx_types_view.mutable_data());
    config.set_rx_types(alloc_, rx_types_view);
    config.set_tx_types(alloc_, tx_types_view);
    return config;
  }

  fuchsia_net_tun::wire::DeviceConfig DefaultDeviceConfig() {
    fuchsia_net_tun::wire::DeviceConfig config(alloc_);
    config.set_base(alloc_, DefaultBaseConfig());
    config.set_mac(alloc_, fuchsia_net::wire::MacAddress{0x01, 0x02, 0x03, 0x04, 0x05, 0x06});
    config.set_blocking(alloc_, true);
    return config;
  }

  fuchsia_net_tun::wire::DevicePairConfig DefaultDevicePairConfig() {
    fuchsia_net_tun::wire::DevicePairConfig config(alloc_);
    config.set_base(alloc_, DefaultBaseConfig());
    config.set_mac_left(alloc_, fuchsia_net::wire::MacAddress{0x01, 0x02, 0x03, 0x04, 0x05, 0x06});
    config.set_mac_right(alloc_, fuchsia_net::wire::MacAddress{0x01, 0x02, 0x03, 0x04, 0x05, 0x07});
    return config;
  }

  zx::status<fidl::ClientEnd<fuchsia_net_tun::Device>> CreateDevice(
      fuchsia_net_tun::wire::DeviceConfig config) {
    zx::status endpoints = fidl::CreateEndpoints<fuchsia_net_tun::Device>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }
    zx::status tun = Connect();
    if (tun.is_error()) {
      return tun.take_error();
    }
    fidl::WireResult result =
        tun.value().CreateDevice(std::move(config), std::move(endpoints->server));
    if (!result.ok()) {
      return zx::error(result.status());
    }
    return zx::ok(std::move(endpoints->client));
  }

  zx::status<fidl::ClientEnd<fuchsia_net_tun::DevicePair>> CreatePair(
      fuchsia_net_tun::wire::DevicePairConfig config) {
    zx::status endpoints = fidl::CreateEndpoints<fuchsia_net_tun::DevicePair>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }
    zx::status tun = Connect();
    if (tun.is_error()) {
      return tun.take_error();
    }
    fidl::WireResult result =
        tun.value().CreatePair(std::move(config), std::move(endpoints->server));
    if (!result.ok()) {
      return zx::error(result.status());
    }
    return zx::ok(std::move(endpoints->client));
  }

  DeviceAdapter& first_adapter() { return *tun_ctl_.devices().front().adapter(); }

  async::Loop tun_ctl_loop_;
  TunCtl tun_ctl_;
  fidl::FidlAllocator<> alloc_;
};

template <class T>
class CapturingEventHandler : public fidl::WireAsyncEventHandler<T> {
 public:
  virtual void Unbound(fidl::UnbindInfo info) override { info_ = info; }

  std::optional<fidl::UnbindInfo> info_;
};

TEST_F(TunTest, InvalidConfigs) {
  auto wait_for_error = [this](fuchsia_net_tun::wire::DeviceConfig config) -> zx_status_t {
    zx::status device = CreateDevice(std::move(config));
    if (device.is_error()) {
      return device.status_value();
    }
    std::shared_ptr handler = std::make_shared<CapturingEventHandler<fuchsia_net_tun::Device>>();
    fidl::Client client(std::move(device.value()), dispatcher(), handler);
    if (!RunLoopWithTimeoutOrUntil([handler] { return handler->info_.has_value(); }, kTimeout)) {
      return ZX_ERR_TIMED_OUT;
    }
    return handler->info_.value().status();
  };

  // Zero MTU
  {
    auto config = DefaultDeviceConfig();
    config.base().set_mtu(alloc_, 0);
    ASSERT_STATUS(wait_for_error(std::move(config)), ZX_ERR_INVALID_ARGS);
  }

  // MTU too large
  {
    auto config = DefaultDeviceConfig();
    config.base().set_mtu(alloc_, fuchsia_net_tun::wire::kMaxMtu + 1);
    ASSERT_STATUS(wait_for_error(std::move(config)), ZX_ERR_INVALID_ARGS);
  }

  // No Rx frames
  {
    auto config = DefaultDeviceConfig();
    config.base().set_rx_types(nullptr);
    ASSERT_STATUS(wait_for_error(std::move(config)), ZX_ERR_INVALID_ARGS);
  }

  // No Tx frames
  {
    auto config = DefaultDeviceConfig();
    config.base().set_tx_types(nullptr);
    ASSERT_STATUS(wait_for_error(std::move(config)), ZX_ERR_INVALID_ARGS);
  }

  // Empty Rx frames
  {
    auto config = DefaultDeviceConfig();
    config.base().rx_types().set_count(0);
    ASSERT_STATUS(wait_for_error(std::move(config)), ZX_ERR_INVALID_ARGS);
  }

  // Empty Tx frames
  {
    auto config = DefaultDeviceConfig();
    config.base().tx_types().set_count(0);
    ASSERT_STATUS(wait_for_error(std::move(config)), ZX_ERR_INVALID_ARGS);
  }
}

TEST_F(TunTest, ConnectNetworkDevice) {
  zx::status device_endpoints = fidl::CreateEndpoints<fuchsia_hardware_network::Device>();
  ASSERT_OK(device_endpoints.status_value());

  fuchsia_net_tun::wire::Protocols protos(alloc_);
  protos.set_network_device(alloc_, std::move(device_endpoints->server));

  zx::status client_end = CreateDevice(DefaultDeviceConfig());
  ASSERT_OK(client_end.status_value());
  fidl::WireSyncClient tun = fidl::BindSyncClient(std::move(client_end.value()));
  ASSERT_OK(tun.ConnectProtocols(std::move(protos)).status());

  fidl::WireSyncClient device = fidl::BindSyncClient(std::move(device_endpoints->client));
  fidl::WireResult info_result = device.GetInfo();
  ASSERT_OK(info_result.status());
}

TEST_F(TunTest, Teardown) {
  zx::status device_endpoints = fidl::CreateEndpoints<fuchsia_hardware_network::Device>();
  ASSERT_OK(device_endpoints.status_value());
  zx::status mac_endpoints = fidl::CreateEndpoints<fuchsia_hardware_network::MacAddressing>();
  ASSERT_OK(mac_endpoints.status_value());

  fuchsia_net_tun::wire::Protocols protos(alloc_);
  protos.set_network_device(alloc_, std::move(device_endpoints->server));
  protos.set_mac_addressing(alloc_, std::move(mac_endpoints->server));

  zx::status client_end = CreateDevice(DefaultDeviceConfig());
  ASSERT_OK(client_end.status_value());
  fidl::WireSyncClient tun = fidl::BindSyncClient(std::move(client_end.value()));
  ASSERT_OK(tun.ConnectProtocols(std::move(protos)).status());

  std::shared_ptr device_handler =
      std::make_shared<CapturingEventHandler<fuchsia_hardware_network::Device>>();
  std::shared_ptr mac_handler =
      std::make_shared<CapturingEventHandler<fuchsia_hardware_network::MacAddressing>>();

  fidl::Client device(std::move(device_endpoints->client), dispatcher(), device_handler);
  fidl::Client mac(std::move(mac_endpoints->client), dispatcher(), mac_handler);

  // get rid of tun.
  tun.client_end().reset();
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&device_handler, &mac_handler]() {
        return device_handler->info_.has_value() && mac_handler->info_.has_value();
      },
      kTimeout, zx::duration::infinite()))
      << "Timed out waiting for channels to close; device_dead="
      << device_handler->info_.has_value() << ", mac_dead=" << mac_handler->info_.has_value();
  ASSERT_STATUS(device_handler->info_.value().status(), ZX_ERR_PEER_CLOSED);
  ASSERT_STATUS(mac_handler->info_.value().status(), ZX_ERR_PEER_CLOSED);
}

TEST_F(TunTest, Status) {
  zx::status device_endpoints = fidl::CreateEndpoints<fuchsia_hardware_network::Device>();
  ASSERT_OK(device_endpoints.status_value());

  fuchsia_net_tun::wire::Protocols protos(alloc_);
  protos.set_network_device(alloc_, std::move(device_endpoints->server));

  zx::status client_end = CreateDevice(DefaultDeviceConfig());
  ASSERT_OK(client_end.status_value());
  fidl::WireSyncClient tun = fidl::BindSyncClient(std::move(client_end.value()));
  ASSERT_OK(tun.ConnectProtocols(std::move(protos)).status());
  fidl::WireSyncClient device = fidl::BindSyncClient(std::move(device_endpoints->client));

  zx::status port_endpoints = fidl::CreateEndpoints<fuchsia_hardware_network::Port>();
  ASSERT_OK(port_endpoints.status_value());
  {
    fidl::WireResult result = device.GetPort(kPort0, std::move(port_endpoints->server));
    ASSERT_OK(result.status());
  }
  fidl::WireSyncClient port = fidl::BindSyncClient(std::move(port_endpoints->client));

  fidl::WireResult status_result = port.GetStatus();
  ASSERT_OK(status_result.status());
  {
    fuchsia_hardware_network::wire::PortStatus port_status = status_result.value().status;
    ASSERT_EQ(port_status.mtu(), kDefaultMtu);
    ASSERT_EQ(port_status.flags(), fuchsia_hardware_network::wire::StatusFlags());
  }

  zx::status watcher_status = GetStatusWatcher(device.client_end(), kPort0, 5);
  ASSERT_OK(watcher_status.status_value());
  fidl::WireSyncClient watcher = fidl::BindSyncClient(std::move(watcher_status.value()));
  {
    fidl::WireResult watch_status_result = watcher.WatchStatus();
    ASSERT_OK(watch_status_result.status());
    fuchsia_hardware_network::wire::PortStatus port_status =
        watch_status_result.value().port_status;
    ASSERT_EQ(port_status.mtu(), kDefaultMtu);
    ASSERT_EQ(port_status.flags(), fuchsia_hardware_network::wire::StatusFlags());
  }

  ASSERT_OK(tun.SetOnline(true).status());

  {
    fidl::WireResult watch_status_result = watcher.WatchStatus();
    ASSERT_OK(watch_status_result.status());
    fuchsia_hardware_network::wire::PortStatus port_status =
        watch_status_result.value().port_status;
    ASSERT_EQ(port_status.mtu(), kDefaultMtu);
    ASSERT_EQ(port_status.flags(), fuchsia_hardware_network::wire::StatusFlags::kOnline);
  }
}

MATCHER(MacEq, "") {
  auto [left, right] = arg;

  return std::equal(left.octets.begin(), left.octets.end(), right.octets.begin(),
                    right.octets.end());
}

MATCHER_P(MacEq, value, "") {
  return std::equal(arg.octets.begin(), arg.octets.end(), value.octets.begin(), value.octets.end());
}

enum class MacSource {
  ConnectProtocols = 1,
  Port = 2,
};

class MacSourceParamTest : public TunTest, public ::testing::WithParamInterface<MacSource> {
 protected:
  zx::status<fidl::ClientEnd<fuchsia_hardware_network::MacAddressing>> OpenMacAddressing(
      fidl::WireSyncClient<fuchsia_net_tun::Device>& tun) {
    zx::status endpoints = fidl::CreateEndpoints<fuchsia_hardware_network::MacAddressing>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }
    fuchsia_net_tun::wire::Protocols protos(alloc_);
    switch (GetParam()) {
      case MacSource::ConnectProtocols:
        protos.set_mac_addressing(alloc_, std::move(endpoints->server));
        break;
      case MacSource::Port: {
        zx::status device = fidl::CreateEndpoints<fuchsia_hardware_network::Device>();
        if (device.is_error()) {
          return device.take_error();
        }
        fuchsia_net_tun::wire::Protocols protos(alloc_);
        protos.set_network_device(alloc_, std::move(device->server));

        zx::status port = fidl::CreateEndpoints<fuchsia_hardware_network::Port>();
        if (port.is_error()) {
          return port.take_error();
        }
        if (zx_status_t status = tun.ConnectProtocols(std::move(protos)).status();
            status != ZX_OK) {
          return zx::error(status);
        }
        if (zx_status_t status = fidl::WireCall(device->client)
                                     .GetPort(DeviceAdapter::kPort0, std::move(port->server))
                                     .status();
            status != ZX_OK) {
          return zx::error(status);
        }
        if (zx_status_t status =
                fidl::WireCall(port->client).GetMac(std::move(endpoints->server)).status();
            status != ZX_OK) {
          return zx::error(status);
        }
      } break;
    }
    if (zx_status_t status = tun.ConnectProtocols(std::move(protos)).status(); status != ZX_OK) {
      return zx::error(status);
    }
    return zx::ok(std::move(endpoints->client));
  }
};

const std::string rxTxParamTestToString(const ::testing::TestParamInfo<MacSource>& info) {
  switch (info.param) {
    case MacSource::ConnectProtocols:
      return "ConnectProtocols";
    case MacSource::Port:
      return "Port";
  }
}

TEST_P(MacSourceParamTest, Mac) {
  fuchsia_net_tun::wire::DeviceConfig config = DefaultDeviceConfig();
  fuchsia_net::wire::MacAddress unicast = config.mac();

  zx::status client_end = CreateDevice(std::move(config));
  ASSERT_OK(client_end.status_value());
  fidl::WireSyncClient tun = fidl::BindSyncClient(std::move(client_end.value()));

  zx::status mac_status = OpenMacAddressing(tun);
  ASSERT_OK(mac_status.status_value());
  fidl::WireSyncClient mac = fidl::BindSyncClient(std::move(mac_status.value()));

  fidl::WireResult get_unicast_address_result = mac.GetUnicastAddress();
  ASSERT_OK(get_unicast_address_result.status());
  ASSERT_THAT(get_unicast_address_result.value().address, MacEq(unicast));

  {
    fidl::WireResult watch_state_result = tun.WatchState();
    ASSERT_OK(watch_state_result.status());
    fuchsia_net_tun::wire::InternalState internal_state = watch_state_result.value().state;
    ASSERT_TRUE(internal_state.has_mac());
    ASSERT_EQ(internal_state.mac().mode(),
              fuchsia_hardware_network::wire::MacFilterMode::kMulticastFilter);
  }

  fuchsia_net::wire::MacAddress multicast{1, 10, 20, 30, 40, 50};
  ASSERT_OK(mac.AddMulticastAddress(multicast).status());

  {
    fidl::WireResult watch_state_result = tun.WatchState();
    ASSERT_OK(watch_state_result.status());
    fuchsia_net_tun::wire::InternalState internal_state = watch_state_result.value().state;
    ASSERT_TRUE(internal_state.has_mac());
    ASSERT_EQ(internal_state.mac().mode(),
              fuchsia_hardware_network::wire::MacFilterMode::kMulticastFilter);
    std::vector<fuchsia_net::wire::MacAddress> multicast_filters;
    std::copy_n(internal_state.mac().multicast_filters().data(),
                internal_state.mac().multicast_filters().count(),
                std::back_inserter(multicast_filters));
    ASSERT_THAT(multicast_filters, ::testing::Pointwise(MacEq(), {multicast}));
  }

  ASSERT_OK(mac.SetMode(fuchsia_hardware_network::wire::MacFilterMode::kPromiscuous).status());

  {
    fidl::WireResult watch_state_result = tun.WatchState();
    ASSERT_OK(watch_state_result.status());
    fuchsia_net_tun::wire::InternalState internal_state = watch_state_result.value().state;
    ASSERT_TRUE(internal_state.has_mac());
    ASSERT_EQ(internal_state.mac().mode(),
              fuchsia_hardware_network::wire::MacFilterMode::kPromiscuous);
    std::vector<fuchsia_net::wire::MacAddress> multicast_filters;
    std::copy_n(internal_state.mac().multicast_filters().data(),
                internal_state.mac().multicast_filters().count(),
                std::back_inserter(multicast_filters));
    ASSERT_THAT(multicast_filters, ::testing::IsEmpty());
  }
}

TEST_P(MacSourceParamTest, NoMac) {
  fuchsia_net_tun::wire::DeviceConfig config = DefaultDeviceConfig();
  // Remove mac information.
  config.set_mac(nullptr);

  zx::status client_end = CreateDevice(std::move(config));
  ASSERT_OK(client_end.status_value());
  fidl::WireSyncClient tun = fidl::BindSyncClient(std::move(client_end.value()));

  zx::status mac_status = OpenMacAddressing(tun);
  ASSERT_OK(mac_status.status_value());

  // Mac channel should be closed because we created tun without a mac information.
  // Wait for the error handler to report that back to us.
  std::shared_ptr mac_handler =
      std::make_shared<CapturingEventHandler<fuchsia_hardware_network::MacAddressing>>();
  fidl::Client mac(std::move(mac_status.value()), dispatcher(), mac_handler);

  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&mac_handler]() { return mac_handler->info_.has_value(); },
                                        kTimeout, zx::duration::infinite()));

  fidl::WireResult get_state_result = tun.GetState();
  ASSERT_OK(get_state_result.status());
  ASSERT_FALSE(get_state_result.value().state.has_mac());
}

INSTANTIATE_TEST_SUITE_P(TunTest, MacSourceParamTest,
                         ::testing::Values(MacSource::ConnectProtocols, MacSource::Port),
                         rxTxParamTestToString);

TEST_F(TunTest, SimpleRxTx) {
  fuchsia_net_tun::wire::DeviceConfig config = DefaultDeviceConfig();
  config.set_online(alloc_, true);
  config.set_blocking(alloc_, false);

  zx::status client_end = CreateDevice(std::move(config));
  ASSERT_OK(client_end.status_value());
  fidl::WireSyncClient tun = fidl::BindSyncClient(std::move(client_end.value()));

  SimpleClient client;
  zx::status protos = client.NewRequest();
  ASSERT_OK(protos.status_value());
  ASSERT_OK(tun.ConnectProtocols(std::move(protos.value())).status());
  ASSERT_OK(client.OpenSession());
  ASSERT_OK(client.AttachPort(kPort0));

  fidl::WireResult get_signals_result = tun.GetSignals();
  ASSERT_OK(get_signals_result.status());
  zx::eventpair& signals = get_signals_result.value().signals;

  // Attempting to read frame without any available buffers should fail with should_wait and the
  // readable signal should not be set.
  {
    fidl::WireResult read_frame_wire_result = tun.ReadFrame();
    ASSERT_OK(read_frame_wire_result.status());
    fuchsia_net_tun::wire::DeviceReadFrameResult read_frame_result =
        read_frame_wire_result.value().result;
    switch (read_frame_result.which()) {
      case fuchsia_net_tun::wire::DeviceReadFrameResult::Tag::kResponse:
        GTEST_FAIL() << "Got frame with " << read_frame_result.response().frame.data().count()
                     << "bytes, expected error";
        break;
      case fuchsia_net_tun::wire::DeviceReadFrameResult::Tag::kErr:
        ASSERT_STATUS(read_frame_result.err(), ZX_ERR_SHOULD_WAIT);
        break;
    }
    ASSERT_STATUS(signals.wait_one(static_cast<uint32_t>(fuchsia_net_tun::wire::Signals::kReadable),
                                   zx::time::infinite_past(), nullptr),
                  ZX_ERR_TIMED_OUT);
  }

  ASSERT_OK(client.SendTx({0x00, 0x01}, true));
  ASSERT_OK(signals.wait_one(static_cast<uint32_t>(fuchsia_net_tun::wire::Signals::kReadable),
                             zx::deadline_after(kTimeout), nullptr));

  {
    fidl::WireResult read_frame_wire_result = tun.ReadFrame();
    ASSERT_OK(read_frame_wire_result.status());
    fuchsia_net_tun::wire::DeviceReadFrameResult read_frame_result =
        read_frame_wire_result.value().result;
    switch (read_frame_result.which()) {
      case fuchsia_net_tun::wire::DeviceReadFrameResult::Tag::kResponse:
        ASSERT_EQ(read_frame_result.response().frame.frame_type(),
                  fuchsia_hardware_network::wire::FrameType::kEthernet);
        ASSERT_NO_FATAL_FAILURE(
            SimpleClient::ValidateData(read_frame_result.response().frame.data(), 0x00));
        ASSERT_FALSE(read_frame_result.response().frame.has_meta());
        break;
      case fuchsia_net_tun::wire::DeviceReadFrameResult::Tag::kErr:
        GTEST_FAIL() << "ReadFrame failed: " << zx_status_get_string(read_frame_result.err());
        break;
    }
  }

  // After read frame, the first descriptor must've been returned.
  uint16_t desc;
  ASSERT_OK(client.FetchTx(&desc));
  EXPECT_EQ(desc, 0x00);

  // Attempting to send a frame without any available buffers should fail with should_wait and the
  // writable signal should not be set.
  fuchsia_net_tun::wire::DeviceWriteFrameResult write_frame_result;
  {
    fuchsia_net_tun::wire::Frame frame(alloc_);
    frame.set_frame_type(alloc_, fuchsia_hardware_network::wire::FrameType::kEthernet);
    uint8_t data[] = {0xAA, 0xBB};
    frame.set_data(alloc_, fidl::VectorView<uint8_t>::FromExternal(data));
    fidl::WireResult write_frame_wire_result = tun.WriteFrame(std::move(frame));
    ASSERT_OK(write_frame_wire_result.status());

    fuchsia_net_tun::wire::DeviceWriteFrameResult write_frame_result =
        write_frame_wire_result.value().result;
    switch (write_frame_result.which()) {
      case fuchsia_net_tun::wire::DeviceWriteFrameResult::Tag::kResponse:
        GTEST_FAIL() << "WriteFrame succeeded unexpectedly";
        break;
      case fuchsia_net_tun::wire::DeviceWriteFrameResult::Tag::kErr:
        ASSERT_STATUS(write_frame_result.err(), ZX_ERR_SHOULD_WAIT);
        ASSERT_STATUS(
            signals.wait_one(static_cast<uint32_t>(fuchsia_net_tun::wire::Signals::kWritable),
                             zx::time::infinite_past(), nullptr),
            ZX_ERR_TIMED_OUT);
        break;
    }
  }

  ASSERT_OK(client.SendRx({0x02}, true));

  // But if we sent stuff out, now it should work after waiting for the available signal.
  ASSERT_OK(signals.wait_one(static_cast<uint32_t>(fuchsia_net_tun::wire::Signals::kWritable),
                             zx::deadline_after(kTimeout), nullptr));
  {
    fuchsia_net_tun::wire::Frame frame(alloc_);
    frame.set_frame_type(alloc_, fuchsia_hardware_network::wire::FrameType::kEthernet);
    uint8_t data[] = {0xAA, 0xBB};
    frame.set_data(alloc_, fidl::VectorView<uint8_t>::FromExternal(data));
    fidl::WireResult write_frame_wire_result = tun.WriteFrame(std::move(frame));
    ASSERT_OK(write_frame_wire_result.status());

    fuchsia_net_tun::wire::DeviceWriteFrameResult write_frame_result =
        write_frame_wire_result.value().result;
    switch (write_frame_result.which()) {
      case fuchsia_net_tun::wire::DeviceWriteFrameResult::Tag::kResponse:
        break;
      case fuchsia_net_tun::wire::DeviceWriteFrameResult::Tag::kErr:
        GTEST_FAIL() << "WriteFrame failed: " << zx_status_get_string(write_frame_result.err());
        break;
    }
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
  SimpleClient left, right;

  zx::status left_request = left.NewRequest();
  ASSERT_OK(left_request.status_value());
  zx::status right_request = right.NewRequest();
  ASSERT_OK(right_request.status_value());

  fuchsia_net_tun::wire::DevicePairEnds ends(alloc_);
  ends.set_left(alloc_, std::move(left_request.value()));
  ends.set_right(alloc_, std::move(right_request.value()));

  zx::status client_end = CreatePair(DefaultDevicePairConfig());
  ASSERT_OK(client_end.status_value());
  fidl::WireSyncClient device_pair = fidl::BindSyncClient(std::move(client_end.value()));
  ASSERT_OK(device_pair.ConnectProtocols(std::move(ends)).status());

  ASSERT_OK(left.OpenSession());
  ASSERT_OK(right.OpenSession());

  ASSERT_OK(right.SendRx({0x05, 0x06, 0x07}, true));
  ASSERT_OK(right.AttachPort(kPort0));
  ASSERT_OK(left.AttachPort(kPort0));
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
  SimpleClient left, right;

  zx::status left_request = left.NewRequest();
  ASSERT_OK(left_request.status_value());
  zx::status right_request = right.NewRequest();
  ASSERT_OK(right_request.status_value());

  fuchsia_net_tun::wire::DevicePairEnds ends(alloc_);
  ends.set_left(alloc_, std::move(left_request.value()));
  ends.set_right(alloc_, std::move(right_request.value()));

  zx::status client_end = CreatePair(DefaultDevicePairConfig());
  ASSERT_OK(client_end.status_value());
  fidl::WireSyncClient device_pair = fidl::BindSyncClient(std::move(client_end.value()));
  ASSERT_OK(device_pair.ConnectProtocols(std::move(ends)).status());

  ASSERT_OK(left.OpenSession());
  ASSERT_OK(right.OpenSession());

  constexpr uint32_t kWatcherBufferLength = 2;

  // Online status should be false for both sides before a session is opened.
  zx::status left_watcher_status =
      GetStatusWatcher(left.device().client_end(), kPort0, kWatcherBufferLength);
  ASSERT_OK(left_watcher_status.status_value());
  fidl::WireSyncClient left_watcher = fidl::BindSyncClient(std::move(left_watcher_status.value()));
  zx::status right_watcher_status =
      GetStatusWatcher(right.device().client_end(), kPort0, kWatcherBufferLength);
  ASSERT_OK(right_watcher_status.status_value());
  fidl::WireSyncClient right_watcher =
      fidl::BindSyncClient(std::move(right_watcher_status.value()));

  {
    fidl::WireResult left_watch_status_result = left_watcher.WatchStatus();
    ASSERT_OK(left_watch_status_result.status());
    fuchsia_hardware_network::wire::PortStatus status_left =
        left_watch_status_result.value().port_status;
    EXPECT_EQ(status_left.flags() & fuchsia_hardware_network::wire::StatusFlags::kOnline,
              fuchsia_hardware_network::wire::StatusFlags());

    fidl::WireResult right_watch_status_result = right_watcher.WatchStatus();
    ASSERT_OK(right_watch_status_result.status());
    fuchsia_hardware_network::wire::PortStatus status_right =
        right_watch_status_result.value().port_status;
    EXPECT_EQ(status_right.flags() & fuchsia_hardware_network::wire::StatusFlags::kOnline,
              fuchsia_hardware_network::wire::StatusFlags());
  }

  // When both sessions are unpaused, online signal must come up.
  ASSERT_OK(left.AttachPort(kPort0));
  ASSERT_OK(right.AttachPort(kPort0));

  {
    fidl::WireResult left_watch_status_result = left_watcher.WatchStatus();
    ASSERT_OK(left_watch_status_result.status());
    fuchsia_hardware_network::wire::PortStatus status_left =
        left_watch_status_result.value().port_status;
    EXPECT_EQ(status_left.flags() & fuchsia_hardware_network::wire::StatusFlags::kOnline,
              fuchsia_hardware_network::wire::StatusFlags::kOnline);

    fidl::WireResult right_watch_status_result = right_watcher.WatchStatus();
    ASSERT_OK(right_watch_status_result.status());
    fuchsia_hardware_network::wire::PortStatus status_right =
        right_watch_status_result.value().port_status;
    EXPECT_EQ(status_right.flags() & fuchsia_hardware_network::wire::StatusFlags::kOnline,
              fuchsia_hardware_network::wire::StatusFlags::kOnline);
  }
}

TEST_F(TunTest, PairFallibleWrites) {
  SimpleClient left, right;

  zx::status left_request = left.NewRequest();
  ASSERT_OK(left_request.status_value());
  zx::status right_request = right.NewRequest();
  ASSERT_OK(right_request.status_value());

  fuchsia_net_tun::wire::DevicePairEnds ends(alloc_);
  ends.set_left(alloc_, std::move(left_request.value()));
  ends.set_right(alloc_, std::move(right_request.value()));

  fuchsia_net_tun::wire::DevicePairConfig config = DefaultDevicePairConfig();
  config.set_fallible_transmit_left(alloc_, true);

  zx::status client_end = CreatePair(std::move(config));
  ASSERT_OK(client_end.status_value());
  fidl::WireSyncClient device_pair = fidl::BindSyncClient(std::move(client_end.value()));
  ASSERT_OK(device_pair.ConnectProtocols(std::move(ends)).status());

  ASSERT_OK(left.OpenSession());
  ASSERT_OK(right.OpenSession());
  ASSERT_OK(left.AttachPort(kPort0));
  ASSERT_OK(right.AttachPort(kPort0));
  ASSERT_OK(left.WaitOnline());
  ASSERT_OK(right.WaitOnline());

  ASSERT_OK(left.SendTx({0x00}, true));
  ASSERT_OK(left.WaitTx());
  uint16_t desc;
  ASSERT_OK(left.FetchTx(&desc));
  ASSERT_EQ(desc, 0x00);
  auto* d = left.descriptor(desc);
  auto flags = static_cast<fuchsia_hardware_network::wire::TxReturnFlags>(d->return_flags);
  EXPECT_EQ(flags & fuchsia_hardware_network::wire::TxReturnFlags::kTxRetError,
            fuchsia_hardware_network::wire::TxReturnFlags::kTxRetError);
}

TEST_F(TunTest, PairInfallibleWrites) {
  SimpleClient left, right;

  zx::status left_request = left.NewRequest();
  ASSERT_OK(left_request.status_value());
  zx::status right_request = right.NewRequest();
  ASSERT_OK(right_request.status_value());

  fuchsia_net_tun::wire::DevicePairEnds ends(alloc_);
  ends.set_left(alloc_, std::move(left_request.value()));
  ends.set_right(alloc_, std::move(right_request.value()));

  zx::status client_end = CreatePair(DefaultDevicePairConfig());
  ASSERT_OK(client_end.status_value());
  fidl::WireSyncClient device_pair = fidl::BindSyncClient(std::move(client_end.value()));
  ASSERT_OK(device_pair.ConnectProtocols(std::move(ends)).status());

  ASSERT_OK(left.OpenSession());
  ASSERT_OK(right.OpenSession());
  ASSERT_OK(left.AttachPort(kPort0));
  ASSERT_OK(right.AttachPort(kPort0));
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
  zx::status client_end = CreateDevice(DefaultDeviceConfig());
  ASSERT_OK(client_end.status_value());
  fidl::WireSyncClient tun = fidl::BindSyncClient(std::move(client_end.value()));

  SimpleClient client;
  zx::status protos = client.NewRequest();
  ASSERT_OK(protos.status_value());
  ASSERT_OK(tun.ConnectProtocols(std::move(protos.value())).status());
  ASSERT_OK(client.OpenSession());
  ASSERT_OK(client.AttachPort(kPort0));

  // Can't send from the tun end.
  {
    fuchsia_net_tun::wire::Frame frame(alloc_);
    frame.set_frame_type(alloc_, fuchsia_hardware_network::wire::FrameType::kEthernet);
    uint8_t data[] = {0x01, 0x02, 0x03};
    frame.set_data(alloc_, fidl::VectorView<uint8_t>::FromExternal(data));
    fidl::WireResult write_frame_wire_result = tun.WriteFrame(std::move(frame));
    ASSERT_OK(write_frame_wire_result.status());

    fuchsia_net_tun::wire::DeviceWriteFrameResult write_frame_result =
        write_frame_wire_result.value().result;
    switch (write_frame_result.which()) {
      case fuchsia_net_tun::wire::DeviceWriteFrameResult::Tag::kResponse:
        GTEST_FAIL() << "WriteFrame succeeded unexpectedly";
        break;
      case fuchsia_net_tun::wire::DeviceWriteFrameResult::Tag::kErr:
        ASSERT_STATUS(write_frame_result.err(), ZX_ERR_BAD_STATE);
        break;
    }
  }
  // Can't send from client end.
  {
    ASSERT_OK(client.SendTx({0x00}, true));
    ASSERT_OK(client.WaitTx());
    uint16_t desc;
    ASSERT_OK(client.FetchTx(&desc));
    ASSERT_EQ(desc, 0x00);
    ASSERT_TRUE(
        fuchsia_hardware_network::wire::TxReturnFlags(client.descriptor(desc)->return_flags) &
        fuchsia_hardware_network::wire::TxReturnFlags::kTxRetError)
        << "Bad return flags " << client.descriptor(desc)->return_flags;
  }
  // If we set online we'll be able to send.
  ASSERT_OK(tun.SetOnline(true).status());

  // Send from client end once more and read a single frame.
  {
    ASSERT_OK(client.SendTx({0x00, 0x01}, true));

    fidl::WireResult read_frame_wire_result = tun.ReadFrame();
    ASSERT_OK(read_frame_wire_result.status());
    fuchsia_net_tun::wire::DeviceReadFrameResult read_frame_result =
        read_frame_wire_result.value().result;
    switch (read_frame_result.which()) {
      case fuchsia_net_tun::wire::DeviceReadFrameResult::Tag::kResponse: {
        ASSERT_EQ(read_frame_result.response().frame.frame_type(),
                  fuchsia_hardware_network::wire::FrameType::kEthernet);
        ASSERT_NO_FATAL_FAILURE(
            SimpleClient::ValidateData(read_frame_result.response().frame.data(), 0x00));
        ASSERT_FALSE(read_frame_result.response().frame.has_meta());
      } break;
      case fuchsia_net_tun::wire::DeviceReadFrameResult::Tag::kErr:
        GTEST_FAIL() << "ReadFrame failed: " << zx_status_get_string(read_frame_result.err());
        break;
    }
  }
  // Set offline and see if client received their tx buffers back.
  ASSERT_OK(tun.SetOnline(false).status());

  uint16_t desc;
  ASSERT_OK(client.WaitTx());
  // No error on first descriptor.
  ASSERT_OK(client.FetchTx(&desc));
  ASSERT_EQ(desc, 0x00);
  EXPECT_FALSE(
      fuchsia_hardware_network::wire::TxReturnFlags(client.descriptor(desc)->return_flags) &
      fuchsia_hardware_network::wire::TxReturnFlags::kTxRetError)
      << "Bad return flags " << client.descriptor(desc)->return_flags;
  // Error on second.
  ASSERT_OK(client.FetchTx(&desc));
  ASSERT_EQ(desc, 0x01);
  EXPECT_TRUE(fuchsia_hardware_network::wire::TxReturnFlags(client.descriptor(desc)->return_flags) &
              fuchsia_hardware_network::wire::TxReturnFlags::kTxRetError)
      << "Bad return flags " << client.descriptor(desc)->return_flags;
}

TEST_F(TunTest, PairEcho) {
  SimpleClient left, right;

  zx::status left_request = left.NewRequest();
  ASSERT_OK(left_request.status_value());
  zx::status right_request = right.NewRequest();
  ASSERT_OK(right_request.status_value());

  fuchsia_net_tun::wire::DevicePairEnds ends(alloc_);
  ends.set_left(alloc_, std::move(left_request.value()));
  ends.set_right(alloc_, std::move(right_request.value()));

  zx::status client_end = CreatePair(DefaultDevicePairConfig());
  ASSERT_OK(client_end.status_value());
  fidl::WireSyncClient device_pair = fidl::BindSyncClient(std::move(client_end.value()));
  ASSERT_OK(device_pair.ConnectProtocols(std::move(ends)).status());

  ASSERT_OK(left.OpenSession());
  ASSERT_OK(right.OpenSession());
  ASSERT_OK(left.AttachPort(kPort0));
  ASSERT_OK(right.AttachPort(kPort0));
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

TEST_F(TunTest, ReportsInternalTxErrors) {
  fuchsia_net_tun::wire::DeviceConfig config = DefaultDeviceConfig();
  config.set_online(alloc_, true);
  // We need tun to be nonblocking so we're able to excite the path that attempts to copy a tx
  // buffer into FIDL and fails because we've removed the VMOs. If the call was blocking it'd block
  // forever and we wouldn't be able to use a sync client here.
  config.set_blocking(alloc_, false);
  zx::status client_end = CreateDevice(std::move(config));
  ASSERT_OK(client_end.status_value());
  fidl::WireSyncClient tun = fidl::BindSyncClient(std::move(client_end.value()));

  SimpleClient client;
  zx::status protos = client.NewRequest();
  ASSERT_OK(protos.status_value());
  ASSERT_OK(tun.ConnectProtocols(std::move(protos.value())).status());
  ASSERT_OK(client.OpenSession());
  ASSERT_OK(client.AttachPort(kPort0));

  // Wait for the device to observe the online session. This guarantees the Session's VMO will be
  // installed by the time we're done waiting.
  for (;;) {
    fidl::WireResult watch_state_result = tun.WatchState();
    ASSERT_OK(watch_state_result.status());
    fuchsia_net_tun::wire::InternalState internal_state = watch_state_result.value().state;
    if (internal_state.has_has_session() && internal_state.has_session()) {
      break;
    }
  }

  // Release all VMOs to make copying the buffer fail later.
  DeviceAdapter& adapter = first_adapter();
  for (uint8_t i = 0; i < MAX_VMOS; i++) {
    adapter.NetworkDeviceImplReleaseVmo(i);
  }

  ASSERT_OK(client.SendTx({0x00}, true));
  uint16_t descriptor;
  // Attempt to fetch the buffer back, calling into ReadFrame through FIDL each round to excite the
  // rx path to attempt the copy into the VMO we invalidated and cause the buffer to be returned to
  // the client.
  for (;;) {
    zx_status_t status = client.FetchTx(&descriptor, nullptr, false);
    if (status == ZX_OK) {
      break;
    }
    ASSERT_STATUS(status, ZX_ERR_SHOULD_WAIT);
    fidl::WireResult read_frame_wire_result = tun.ReadFrame();
    ASSERT_OK(read_frame_wire_result.status());
    fuchsia_net_tun::wire::DeviceReadFrameResult read_frame_result =
        read_frame_wire_result.value().result;
    ASSERT_EQ(read_frame_result.which(), fuchsia_net_tun::wire::DeviceReadFrameResult::Tag::kErr);
    ASSERT_STATUS(read_frame_result.err(), ZX_ERR_SHOULD_WAIT);
  }
  const buffer_descriptor_t* desc = client.descriptor(descriptor);
  EXPECT_EQ(desc->return_flags,
            static_cast<uint32_t>(fuchsia_hardware_network::wire::TxReturnFlags::kTxRetError));
}

}  // namespace testing
}  // namespace tun
}  // namespace network
