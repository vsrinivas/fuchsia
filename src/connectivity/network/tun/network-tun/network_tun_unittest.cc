// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/stdcompat/span.h>
#include <lib/sync/completion.h>
#include <lib/syslog/global.h>
#include <lib/zx/time.h>
#include <zircon/device/network.h>
#include <zircon/status.h>

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
constexpr uint8_t kDefaultTestPort = 2;

zx::result<fidl::ClientEnd<fuchsia_hardware_network::StatusWatcher>> GetStatusWatcher(
    const fidl::ClientEnd<fuchsia_hardware_network::Device>& device,
    fuchsia_hardware_network::wire::PortId port, uint32_t buffer) {
  zx::result port_endpoints = fidl::CreateEndpoints<fuchsia_hardware_network::Port>();
  if (port_endpoints.is_error()) {
    return port_endpoints.take_error();
  }
  {
    fidl::WireResult result =
        fidl::WireCall(device)->GetPort(port, std::move(port_endpoints->server));
    if (!result.ok()) {
      return zx::error(result.status());
    }
  }

  zx::result watcher_endpoints = fidl::CreateEndpoints<fuchsia_hardware_network::StatusWatcher>();
  if (watcher_endpoints.is_error()) {
    return watcher_endpoints.take_error();
  }

  {
    fidl::WireResult result = fidl::WireCall(port_endpoints->client)
                                  ->GetStatusWatcher(std::move(watcher_endpoints->server), buffer);
    if (!result.ok()) {
      return zx::error(result.status());
    }
  }

  return zx::ok(std::move(watcher_endpoints->client));
}

zx::result<fidl::ClientEnd<fuchsia_hardware_network::MacAddressing>> GetMacAddressing(
    fidl::WireSyncClient<fuchsia_net_tun::Device>& tun,
    fuchsia_hardware_network::wire::PortId port_id) {
  zx::result endpoints = fidl::CreateEndpoints<fuchsia_hardware_network::MacAddressing>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }

  zx::result device = fidl::CreateEndpoints<fuchsia_hardware_network::Device>();
  if (device.is_error()) {
    return device.take_error();
  }

  zx::result port = fidl::CreateEndpoints<fuchsia_hardware_network::Port>();
  if (port.is_error()) {
    return port.take_error();
  }
  if (zx_status_t status = tun->GetDevice(std::move(device->server)).status(); status != ZX_OK) {
    return zx::error(status);
  }
  if (zx_status_t status =
          fidl::WireCall(device->client)->GetPort(port_id, std::move(port->server)).status();
      status != ZX_OK) {
    return zx::error(status);
  }
  if (zx_status_t status =
          fidl::WireCall(port->client)->GetMac(std::move(endpoints->server)).status();
      status != ZX_OK) {
    return zx::error(status);
  }

  return zx::ok(std::move(endpoints->client));
}

zx::result<fidl::ClientEnd<fuchsia_hardware_network::PortWatcher>> GetPortWatcher(
    fidl::ClientEnd<fuchsia_hardware_network::Device>& device) {
  zx::result endpoints = fidl::CreateEndpoints<fuchsia_hardware_network::PortWatcher>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }
  {
    fidl::WireResult result = fidl::WireCall(device)->GetPortWatcher(std::move(endpoints->server));
    if (!result.ok()) {
      return zx::error(result.status());
    }
  }

  return zx::ok(std::move(endpoints->client));
}

struct OwnedPortEvent {
  OwnedPortEvent(const fuchsia_hardware_network::wire::DevicePortEvent& event)
      : which(event.Which()) {
    switch (event.Which()) {
      case fuchsia_hardware_network::wire::DevicePortEvent::Tag::kExisting:
        port_id = event.existing();
        break;
      case fuchsia_hardware_network::wire::DevicePortEvent::Tag::kAdded:
        port_id = event.added();
        break;
      case fuchsia_hardware_network::wire::DevicePortEvent::Tag::kRemoved:
        port_id = event.removed();
        break;
      case fuchsia_hardware_network::wire::DevicePortEvent::Tag::kIdle:
        break;
    }
  }

  std::string describe() const {
    std::stringstream ss;
    switch (which) {
      case fuchsia_hardware_network::wire::DevicePortEvent::Tag::kExisting:
        ss << "removed";
        break;
      case fuchsia_hardware_network::wire::DevicePortEvent::Tag::kAdded:
        ss << "added";
        break;
      case fuchsia_hardware_network::wire::DevicePortEvent::Tag::kRemoved:
        ss << "removed";
        break;
      case fuchsia_hardware_network::wire::DevicePortEvent::Tag::kIdle:
        ss << "idle";
    }
    if (port_id.has_value()) {
      const fuchsia_hardware_network::wire::PortId& id = port_id.value();
      ss << "(" << static_cast<uint32_t>(id.base) << ";salt=" << static_cast<uint32_t>(id.salt)
         << ")";
    }
    return ss.str();
  }

  fuchsia_hardware_network::wire::DevicePortEvent::Tag which;
  std::optional<fuchsia_hardware_network::wire::PortId> port_id;
};

zx::result<OwnedPortEvent> WatchPorts(
    fidl::WireSyncClient<fuchsia_hardware_network::PortWatcher>& port_watcher) {
  fidl::WireResult result = port_watcher->Watch();
  if (!result.ok()) {
    return zx::error(result.status());
  }
  return zx::ok(OwnedPortEvent(result.value().event));
}

zx::result<fuchsia_hardware_network::wire::PortId> GetPortId(
    fit::function<zx_status_t(fidl::ServerEnd<fuchsia_hardware_network::Port>)> get_port) {
  zx::result endpoints = fidl::CreateEndpoints<fuchsia_hardware_network::Port>();
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

zx::result<fuchsia_hardware_network::wire::PortId> GetPortId(
    const fidl::ClientEnd<fuchsia_net_tun::Port>& tun_port) {
  return GetPortId([&tun_port](fidl::ServerEnd<fuchsia_hardware_network::Port> server) {
    return fidl::WireCall(tun_port)->GetPort(std::move(server)).status();
  });
}

zx::result<
    std::tuple<fuchsia_hardware_network::wire::PortId, fuchsia_hardware_network::wire::PortId>>
GetPairPortIds(uint8_t port_id, const fidl::ClientEnd<fuchsia_net_tun::DevicePair>& tun_pair) {
  zx::result left =
      GetPortId([&tun_pair, &port_id](fidl::ServerEnd<fuchsia_hardware_network::Port> server) {
        return fidl::WireCall(tun_pair)->GetLeftPort(port_id, std::move(server)).status();
      });
  if (left.is_error()) {
    return left.take_error();
  }
  zx::result right =
      GetPortId([&tun_pair, &port_id](fidl::ServerEnd<fuchsia_hardware_network::Port> server) {
        return fidl::WireCall(tun_pair)->GetRightPort(port_id, std::move(server)).status();
      });
  if (right.is_error()) {
    return right.take_error();
  }
  return zx::ok(std::make_tuple(left.value(), right.value()));
}

}  // namespace

constexpr uint32_t kDefaultMtu = 1500;

// A very simple client to fuchsia.hardware.network.Device to run data path
// tests against.
class SimpleClient {
 public:
  static constexpr uint64_t kBufferSize = 2048;

  SimpleClient() = default;

  zx::result<fidl::ServerEnd<fuchsia_hardware_network::Device>> NewRequest() {
    zx::result endpoints = fidl::CreateEndpoints<fuchsia_hardware_network::Device>();
    if (!endpoints.is_ok()) {
      return endpoints.take_error();
    }
    device_ = fidl::WireSyncClient(std::move(endpoints->client));
    return zx::ok(std::move(endpoints->server));
  }

  zx_status_t OpenSession() {
    fidl::WireResult info_result = device()->GetInfo();
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
    session_info.set_descriptor_version(NETWORK_DEVICE_DESCRIPTOR_VERSION);
    session_info.set_descriptor_length(
        static_cast<uint8_t>(sizeof(buffer_descriptor_t) / sizeof(uint64_t)));
    session_info.set_descriptor_count(descriptor_count_);
    session_info.set_options(fuchsia_hardware_network::wire::SessionFlags::kPrimary);

    zx::vmo data;
    if ((status = data_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &data)) != ZX_OK) {
      return status;
    }
    session_info.set_data(std::move(data));

    zx::vmo descriptors;
    if ((status = descriptors_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &descriptors)) != ZX_OK) {
      return status;
    }
    session_info.set_descriptors(std::move(descriptors));

    fidl::WireResult session_result = device()->OpenSession("tun-test", std::move(session_info));
    if (!session_result.ok()) {
      return session_result.status();
    }
    const fit::result<int32_t, ::fuchsia_hardware_network::wire::DeviceOpenSessionResponse*>&
        result = session_result.value();
    if (result.is_error()) {
      return result.error_value();
    }
    fuchsia_hardware_network::wire::DeviceOpenSessionResponse& response = *result.value();
    session_ = fidl::WireSyncClient(std::move(response.session));
    rx_ = std::move(response.fifos.rx);
    tx_ = std::move(response.fifos.tx);
    return ZX_OK;
  }

  zx_status_t AttachPort(fuchsia_hardware_network::wire::PortId port_id,
                         std::vector<fuchsia_hardware_network::wire::FrameType> frames = {
                             fuchsia_hardware_network::wire::FrameType::kEthernet}) {
    fidl::WireResult wire_result = session_->Attach(
        port_id, fidl::VectorView<fuchsia_hardware_network::wire::FrameType>::FromExternal(frames));
    if (!wire_result.ok()) {
      return wire_result.status();
    }
    const auto& result = wire_result.value();
    if (result.is_error()) {
      return result.error_value();
    }
    port_id_ = port_id;
    return ZX_OK;
  }

  buffer_descriptor_t* descriptor(uint16_t index) {
    if (index > descriptor_count_) {
      return nullptr;
    }
    return static_cast<buffer_descriptor_t*>(descriptors_.start()) + index;
  }

  cpp20::span<uint8_t> data(const buffer_descriptor_t* desc) {
    return cpp20::span(static_cast<uint8_t*>(data_.start()) + desc->offset, desc->data_length);
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
        .port_id =
            {
                .base = port_id_.base,
                .salt = port_id_.salt,
            },
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
    return fifo->write(sizeof(uint16_t), descs.data(), count, nullptr);
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

  zx_status_t WaitOnline(fuchsia_hardware_network::wire::PortId port) {
    zx::result watcher = GetStatusWatcher(device().client_end(), port, 5);
    if (watcher.is_error()) {
      return watcher.error_value();
    }
    bool online = false;
    while (!online) {
      fidl::WireResult result = fidl::WireCall(watcher.value())->WatchStatus();
      if (!result.ok()) {
        return result.status();
      }
      const fuchsia_hardware_network::wire::PortStatus status = result.value().port_status;
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
  fidl::Arena<> alloc_;

  fidl::WireSyncClient<fuchsia_hardware_network::Device> device_;
  fuchsia_hardware_network::wire::PortId port_id_;
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

  zx::result<fidl::WireSyncClient<fuchsia_net_tun::Control>> Connect() {
    zx::result endpoints = fidl::CreateEndpoints<fuchsia_net_tun::Control>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }
    tun_ctl_.Connect(std::move(endpoints->server));
    return zx::ok(fidl::WireSyncClient(std::move(endpoints->client)));
  }

  fuchsia_net_tun::wire::BasePortConfig DefaultBasePortConfig() {
    fuchsia_net_tun::wire::BasePortConfig config(alloc_);
    config.set_mtu(kDefaultMtu);
    config.set_id(kDefaultTestPort);
    const fuchsia_hardware_network::wire::FrameType rx_types[] = {
        fuchsia_hardware_network::wire::FrameType::kEthernet,
    };
    fidl::VectorView<fuchsia_hardware_network::wire::FrameType> rx_types_view(alloc_,
                                                                              std::size(rx_types));
    std::copy(std::begin(rx_types), std::end(rx_types), rx_types_view.data());
    const fuchsia_hardware_network::wire::FrameTypeSupport tx_types[] = {
        fuchsia_hardware_network::wire::FrameTypeSupport{
            .type = fuchsia_hardware_network::wire::FrameType::kEthernet,
        },
    };
    fidl::VectorView<fuchsia_hardware_network::wire::FrameTypeSupport> tx_types_view(
        alloc_, std::size(tx_types));
    std::copy(std::begin(tx_types), std::end(tx_types), tx_types_view.data());
    config.set_rx_types(alloc_, rx_types_view);
    config.set_tx_types(alloc_, tx_types_view);
    return config;
  }

  fuchsia_net_tun::wire::DeviceConfig DefaultDeviceConfig() {
    fuchsia_net_tun::wire::DeviceConfig config(alloc_);
    config.set_blocking(true);
    return config;
  }

  fuchsia_net_tun::wire::DevicePairConfig DefaultDevicePairConfig() {
    fuchsia_net_tun::wire::DevicePairConfig config(alloc_);
    return config;
  }

  fuchsia_net_tun::wire::DevicePortConfig DefaultDevicePortConfig() {
    fuchsia_net_tun::wire::DevicePortConfig config(alloc_);
    config.set_base(alloc_, DefaultBasePortConfig());
    config.set_mac(alloc_, fuchsia_net::wire::MacAddress{0x01, 0x02, 0x03, 0x04, 0x05, 0x06});
    return config;
  }

  fuchsia_net_tun::wire::DevicePairPortConfig DefaultDevicePairPortConfig() {
    fuchsia_net_tun::wire::DevicePairPortConfig config(alloc_);
    config.set_base(alloc_, DefaultBasePortConfig());
    config.set_mac_left(alloc_, fuchsia_net::wire::MacAddress{0x01, 0x02, 0x03, 0x04, 0x05, 0x06});
    config.set_mac_right(alloc_, fuchsia_net::wire::MacAddress{0x01, 0x02, 0x03, 0x04, 0x05, 0x07});
    return config;
  }

  zx::result<fidl::ClientEnd<fuchsia_net_tun::Device>> CreateDevice(
      fuchsia_net_tun::wire::DeviceConfig config) {
    zx::result endpoints = fidl::CreateEndpoints<fuchsia_net_tun::Device>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }
    zx::result tun = Connect();
    if (tun.is_error()) {
      return tun.take_error();
    }
    fidl::WireResult result =
        tun.value()->CreateDevice(std::move(config), std::move(endpoints->server));
    if (!result.ok()) {
      return zx::error(result.status());
    }
    return zx::ok(std::move(endpoints->client));
  }

  zx::result<
      std::pair<fidl::ClientEnd<fuchsia_net_tun::Device>, fidl::ClientEnd<fuchsia_net_tun::Port>>>
  CreateDeviceAndPort(fuchsia_net_tun::wire::DeviceConfig device_config,
                      fuchsia_net_tun::wire::DevicePortConfig port_config) {
    zx::result device = CreateDevice(std::move(device_config));
    if (device.is_error()) {
      return device.take_error();
    }
    fidl::WireSyncClient client{std::move(*device)};

    zx::result port_endpoints = fidl::CreateEndpoints<fuchsia_net_tun::Port>();
    if (port_endpoints.is_error()) {
      return port_endpoints.take_error();
    }
    if (zx_status_t status =
            client->AddPort(std::move(port_config), std::move(port_endpoints->server)).status();
        status != ZX_OK) {
      return zx::error(status);
    };

    return zx::ok(std::make_pair(client.TakeClientEnd(), std::move(port_endpoints->client)));
  }

  zx::result<fidl::ClientEnd<fuchsia_net_tun::DevicePair>> CreatePair(
      fuchsia_net_tun::wire::DevicePairConfig config) {
    zx::result endpoints = fidl::CreateEndpoints<fuchsia_net_tun::DevicePair>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }
    zx::result tun = Connect();
    if (tun.is_error()) {
      return tun.take_error();
    }
    fidl::WireResult result =
        tun.value()->CreatePair(std::move(config), std::move(endpoints->server));
    if (!result.ok()) {
      return zx::error(result.status());
    }
    return zx::ok(std::move(endpoints->client));
  }

  zx::result<fidl::ClientEnd<fuchsia_net_tun::DevicePair>> CreatePairAndPort(
      fuchsia_net_tun::wire::DevicePairConfig device_config,
      fuchsia_net_tun::wire::DevicePairPortConfig port_config) {
    zx::result pair_status = CreatePair(std::move(device_config));
    if (pair_status.is_error()) {
      return pair_status.take_error();
    }
    fidl::ClientEnd<fuchsia_net_tun::DevicePair>& pair = pair_status.value();
    fidl::WireResult result = fidl::WireCall(pair)->AddPort(std::move(port_config));
    if (result.status() != ZX_OK) {
      return zx::error(result.status());
    }
    const auto* res = result.Unwrap();
    if (res->is_error()) {
      return zx::error(res->error_value());
    }
    return zx::ok(std::move(pair));
  }

  DeviceAdapter& first_adapter() { return *tun_ctl_.devices().front().adapter(); }

  async::Loop tun_ctl_loop_;
  TunCtl tun_ctl_;
  fidl::Arena<> alloc_;
};

template <class T>
class CapturingEventHandler : public fidl::WireAsyncEventHandler<T> {
 public:
  CapturingEventHandler() = default;
  CapturingEventHandler(const CapturingEventHandler&) = delete;
  CapturingEventHandler(CapturingEventHandler&&) = delete;

  void on_fidl_error(fidl::UnbindInfo info) override { info_ = info; }

  std::optional<fidl::UnbindInfo> info_;
};

TEST_F(TunTest, InvalidPortConfigs) {
  zx::result status = CreateDevice(DefaultDeviceConfig());
  ASSERT_OK(status.status_value());
  fidl::WireSyncClient device{std::move(status.value())};

  auto wait_for_error = [this,
                         &device](fuchsia_net_tun::wire::DevicePortConfig config) -> zx_status_t {
    zx::result port_endpoints = fidl::CreateEndpoints<fuchsia_net_tun::Port>();
    if (port_endpoints.is_error()) {
      return port_endpoints.status_value();
    }

    fidl::WireResult result = device->AddPort(config, std::move(port_endpoints->server));
    if (result.status() != ZX_OK) {
      return result.status();
    }

    CapturingEventHandler<fuchsia_net_tun::Port> handler;
    fidl::WireClient client(std::move(port_endpoints->client), dispatcher(), &handler);

    if (!RunLoopWithTimeoutOrUntil([&handler] { return handler.info_.has_value(); }, kTimeout)) {
      return ZX_ERR_TIMED_OUT;
    }
    return handler.info_.value().status();
  };

  // Zero MTU
  {
    auto config = DefaultDevicePortConfig();
    config.base().set_mtu(0);
    ASSERT_STATUS(wait_for_error(std::move(config)), ZX_ERR_INVALID_ARGS);
  }

  // MTU too large
  {
    auto config = DefaultDevicePortConfig();
    config.base().set_mtu(fuchsia_net_tun::wire::kMaxMtu + 1);
    ASSERT_STATUS(wait_for_error(std::move(config)), ZX_ERR_INVALID_ARGS);
  }

  // No Rx frames
  {
    auto config = DefaultDevicePortConfig();
    config.base().set_rx_types(nullptr);
    ASSERT_STATUS(wait_for_error(std::move(config)), ZX_ERR_INVALID_ARGS);
  }

  // No Tx frames
  {
    auto config = DefaultDevicePortConfig();
    config.base().set_tx_types(nullptr);
    ASSERT_STATUS(wait_for_error(std::move(config)), ZX_ERR_INVALID_ARGS);
  }

  // Empty Rx frames
  {
    auto config = DefaultDevicePortConfig();
    config.base().rx_types().set_count(0);
    ASSERT_STATUS(wait_for_error(std::move(config)), ZX_ERR_INVALID_ARGS);
  }

  // Empty Tx frames
  {
    auto config = DefaultDevicePortConfig();
    config.base().tx_types().set_count(0);
    ASSERT_STATUS(wait_for_error(std::move(config)), ZX_ERR_INVALID_ARGS);
  }
}

TEST_F(TunTest, ConnectNetworkDevice) {
  zx::result device_endpoints = fidl::CreateEndpoints<fuchsia_hardware_network::Device>();
  ASSERT_OK(device_endpoints.status_value());

  zx::result client_end = CreateDevice(DefaultDeviceConfig());
  ASSERT_OK(client_end.status_value());
  fidl::WireSyncClient tun{std::move(client_end.value())};
  ASSERT_OK(tun->GetDevice(std::move(device_endpoints->server)).status());

  fidl::WireSyncClient device{std::move(device_endpoints->client)};
  fidl::WireResult info_result = device->GetInfo();
  ASSERT_OK(info_result.status());
}

TEST_F(TunTest, Teardown) {
  zx::result device_endpoints = fidl::CreateEndpoints<fuchsia_hardware_network::Device>();
  ASSERT_OK(device_endpoints.status_value());
  zx::result port_endpoints = fidl::CreateEndpoints<fuchsia_hardware_network::Port>();
  ASSERT_OK(port_endpoints.status_value());
  zx::result mac_endpoints = fidl::CreateEndpoints<fuchsia_hardware_network::MacAddressing>();
  ASSERT_OK(mac_endpoints.status_value());

  zx::result device_and_port =
      CreateDeviceAndPort(DefaultDeviceConfig(), DefaultDevicePortConfig());
  ASSERT_OK(device_and_port.status_value());
  auto& [device_client_end, port_client_end] = *device_and_port;
  fidl::WireSyncClient tun{std::move(device_client_end)};

  ASSERT_OK(tun->GetDevice(std::move(device_endpoints->server)).status());

  zx::result port_id = GetPortId(port_client_end);
  ASSERT_OK(port_id.status_value());
  ASSERT_OK(fidl::WireCall(device_endpoints->client)
                ->GetPort(port_id.value(), std::move(port_endpoints->server))
                .status());
  ASSERT_OK(
      fidl::WireCall(port_endpoints->client)->GetMac(std::move(mac_endpoints->server)).status());
  // Perform a synchronous call on Mac, which validates all the pipelined calls above succeeded.
  ASSERT_OK(fidl::WireCall(mac_endpoints->client)->GetUnicastAddress().status());

  CapturingEventHandler<fuchsia_hardware_network::Device> device_handler;
  CapturingEventHandler<fuchsia_hardware_network::Port> port_handler;
  CapturingEventHandler<fuchsia_hardware_network::MacAddressing> mac_handler;

  fidl::WireClient device(std::move(device_endpoints->client), dispatcher(), &device_handler);
  fidl::WireClient port(std::move(port_endpoints->client), dispatcher(), &port_handler);
  fidl::WireClient mac(std::move(mac_endpoints->client), dispatcher(), &mac_handler);

  // get rid of tun.
  tun = {};
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&device_handler, &mac_handler, &port_handler]() {
        return device_handler.info_.has_value() && mac_handler.info_.has_value() &&
               port_handler.info_.has_value();
      },
      kTimeout, zx::duration::infinite()))
      << "Timed out waiting for channels to close; device_dead=" << device_handler.info_.has_value()
      << ", mac_dead=" << mac_handler.info_.has_value()
      << ", port_dead=" << port_handler.info_.has_value();
  ASSERT_STATUS(device_handler.info_.value().status(), ZX_ERR_PEER_CLOSED);
  ASSERT_STATUS(mac_handler.info_.value().status(), ZX_ERR_PEER_CLOSED);
  ASSERT_STATUS(port_handler.info_.value().status(), ZX_ERR_PEER_CLOSED);
}

TEST_F(TunTest, Status) {
  zx::result device_endpoints = fidl::CreateEndpoints<fuchsia_hardware_network::Device>();
  ASSERT_OK(device_endpoints.status_value());

  zx::result device_and_port =
      CreateDeviceAndPort(DefaultDeviceConfig(), DefaultDevicePortConfig());
  ASSERT_OK(device_and_port.status_value());
  auto& [device_client_end, port_client_end] = *device_and_port;

  zx::result maybe_port_id = GetPortId(port_client_end);
  ASSERT_OK(maybe_port_id.status_value());
  const netdev::wire::PortId port_id = maybe_port_id.value();

  fidl::WireSyncClient tun{std::move(device_client_end)};
  fidl::WireSyncClient tun_port{std::move(port_client_end)};

  ASSERT_OK(tun->GetDevice(std::move(device_endpoints->server)).status());
  fidl::WireSyncClient device{std::move(device_endpoints->client)};

  zx::result port_endpoints = fidl::CreateEndpoints<fuchsia_hardware_network::Port>();
  ASSERT_OK(port_endpoints.status_value());
  {
    fidl::WireResult result = device->GetPort(port_id, std::move(port_endpoints->server));
    ASSERT_OK(result.status());
  }
  fidl::WireSyncClient port{std::move(port_endpoints->client)};

  fidl::WireResult status_result = port->GetStatus();
  ASSERT_OK(status_result.status());
  {
    const fuchsia_hardware_network::wire::PortStatus port_status = status_result.value().status;
    ASSERT_EQ(port_status.mtu(), kDefaultMtu);
    ASSERT_EQ(port_status.flags(), fuchsia_hardware_network::wire::StatusFlags());
  }

  zx::result watcher_status = GetStatusWatcher(device.client_end(), port_id, 5);
  ASSERT_OK(watcher_status.status_value());
  fidl::WireSyncClient watcher{std::move(watcher_status.value())};
  {
    fidl::WireResult watch_status_result = watcher->WatchStatus();
    ASSERT_OK(watch_status_result.status());
    const fuchsia_hardware_network::wire::PortStatus port_status =
        watch_status_result.value().port_status;
    ASSERT_EQ(port_status.mtu(), kDefaultMtu);
    ASSERT_EQ(port_status.flags(), fuchsia_hardware_network::wire::StatusFlags());
  }

  ASSERT_OK(tun_port->SetOnline(true).status());

  {
    fidl::WireResult watch_status_result = watcher->WatchStatus();
    ASSERT_OK(watch_status_result.status());
    const fuchsia_hardware_network::wire::PortStatus port_status =
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

TEST_F(TunTest, Mac) {
  fuchsia_net_tun::wire::DevicePortConfig port_config = DefaultDevicePortConfig();
  fuchsia_net::wire::MacAddress unicast = port_config.mac();

  zx::result device_and_port = CreateDeviceAndPort(DefaultDeviceConfig(), std::move(port_config));
  ASSERT_OK(device_and_port.status_value());
  auto& [device_client_end, port_client_end] = *device_and_port;

  zx::result maybe_port_id = GetPortId(port_client_end);
  ASSERT_OK(maybe_port_id.status_value());
  const fuchsia_hardware_network::wire::PortId port_id = maybe_port_id.value();

  fidl::WireSyncClient tun{std::move(device_client_end)};
  fidl::WireSyncClient tun_port{std::move(port_client_end)};

  zx::result mac_status = GetMacAddressing(tun, port_id);
  ASSERT_OK(mac_status.status_value());
  fidl::WireSyncClient mac{std::move(mac_status.value())};

  fidl::WireResult get_unicast_address_result = mac->GetUnicastAddress();
  ASSERT_OK(get_unicast_address_result.status());
  ASSERT_THAT(get_unicast_address_result.value().address, MacEq(unicast));

  {
    fidl::WireResult watch_state_result = tun_port->WatchState();
    ASSERT_OK(watch_state_result.status());
    const fuchsia_net_tun::wire::InternalState internal_state = watch_state_result.value().state;
    ASSERT_TRUE(internal_state.has_mac());
    ASSERT_EQ(internal_state.mac().mode(),
              fuchsia_hardware_network::wire::MacFilterMode::kMulticastFilter);
  }

  fuchsia_net::wire::MacAddress multicast{1, 10, 20, 30, 40, 50};
  ASSERT_OK(mac->AddMulticastAddress(multicast).status());

  {
    fidl::WireResult watch_state_result = tun_port->WatchState();
    ASSERT_OK(watch_state_result.status());
    const fuchsia_net_tun::wire::InternalState internal_state = watch_state_result.value().state;
    ASSERT_TRUE(internal_state.has_mac());
    ASSERT_EQ(internal_state.mac().mode(),
              fuchsia_hardware_network::wire::MacFilterMode::kMulticastFilter);
    std::vector<fuchsia_net::wire::MacAddress> multicast_filters;
    std::copy_n(internal_state.mac().multicast_filters().data(),
                internal_state.mac().multicast_filters().count(),
                std::back_inserter(multicast_filters));
    ASSERT_THAT(multicast_filters, ::testing::Pointwise(MacEq(), {multicast}));
  }

  ASSERT_OK(mac->SetMode(fuchsia_hardware_network::wire::MacFilterMode::kPromiscuous).status());

  {
    fidl::WireResult watch_state_result = tun_port->WatchState();
    ASSERT_OK(watch_state_result.status());
    const fuchsia_net_tun::wire::InternalState internal_state = watch_state_result.value().state;
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

TEST_F(TunTest, NoMac) {
  fuchsia_net_tun::wire::DevicePortConfig port_config = DefaultDevicePortConfig();
  // Remove mac information.
  port_config.set_mac(nullptr);

  zx::result device_and_port = CreateDeviceAndPort(DefaultDeviceConfig(), std::move(port_config));
  ASSERT_OK(device_and_port.status_value());
  auto& [device_client_end, port_client_end] = *device_and_port;

  zx::result maybe_port_id = GetPortId(port_client_end);
  ASSERT_OK(maybe_port_id.status_value());
  const fuchsia_hardware_network::wire::PortId port_id = maybe_port_id.value();

  fidl::WireSyncClient tun{std::move(device_client_end)};
  fidl::WireSyncClient tun_port{std::move(port_client_end)};

  zx::result mac_status = GetMacAddressing(tun, port_id);
  ASSERT_OK(mac_status.status_value());

  // Mac channel should be closed because we created tun without a mac information.
  // Wait for the error handler to report that back to us.
  CapturingEventHandler<fuchsia_hardware_network::MacAddressing> mac_handler;
  fidl::WireClient mac(std::move(mac_status.value()), dispatcher(), &mac_handler);

  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&mac_handler]() { return mac_handler.info_.has_value(); },
                                        kTimeout, zx::duration::infinite()));

  fidl::WireResult get_state_result = tun_port->GetState();
  ASSERT_OK(get_state_result.status());
  ASSERT_FALSE(get_state_result.value().state.has_mac());
}

TEST_F(TunTest, SimpleRxTx) {
  fuchsia_net_tun::wire::DeviceConfig device_config = DefaultDeviceConfig();
  fuchsia_net_tun::wire::DevicePortConfig port_config = DefaultDevicePortConfig();
  port_config.set_online(true);
  device_config.set_blocking(false);

  zx::result device_and_port =
      CreateDeviceAndPort(std::move(device_config), std::move(port_config));
  ASSERT_OK(device_and_port.status_value());
  auto [device_client_end, port_client_end] = std::move(device_and_port.value());

  zx::result maybe_port_id = GetPortId(port_client_end);
  ASSERT_OK(maybe_port_id.status_value());
  const fuchsia_hardware_network::wire::PortId port_id = maybe_port_id.value();

  fidl::WireSyncClient tun{std::move(device_client_end)};

  SimpleClient client;
  zx::result request = client.NewRequest();
  ASSERT_OK(request.status_value());
  ASSERT_OK(tun->GetDevice(std::move(request.value())).status());
  ASSERT_OK(client.OpenSession());
  ASSERT_OK(client.AttachPort(port_id));

  fidl::WireResult get_signals_result = tun->GetSignals();
  ASSERT_OK(get_signals_result.status());
  zx::eventpair& signals = get_signals_result.value().signals;

  // Attempting to read frame without any available buffers should fail with should_wait and the
  // readable signal should not be set.
  {
    fidl::WireResult read_frame_wire_result = tun->ReadFrame();
    ASSERT_OK(read_frame_wire_result.status());
    const fit::result<int32_t, ::fuchsia_net_tun::wire::DeviceReadFrameResponse*>&
        read_frame_result = read_frame_wire_result.value();
    if (read_frame_result.is_error()) {
      ASSERT_STATUS(read_frame_result.error_value(), ZX_ERR_SHOULD_WAIT);
    } else {
      GTEST_FAIL() << "Got frame with " << read_frame_result.value()->frame.data().count()
                   << "bytes, expected error";
    }
    ASSERT_STATUS(signals.wait_one(static_cast<uint32_t>(fuchsia_net_tun::wire::Signals::kReadable),
                                   zx::time::infinite_past(), nullptr),
                  ZX_ERR_TIMED_OUT);
  }

  ASSERT_OK(client.SendTx({0x00, 0x01}, true));
  ASSERT_OK(signals.wait_one(static_cast<uint32_t>(fuchsia_net_tun::wire::Signals::kReadable),
                             zx::deadline_after(kTimeout), nullptr));

  {
    fidl::WireResult read_frame_wire_result = tun->ReadFrame();
    ASSERT_OK(read_frame_wire_result.status());
    const fit::result<int32_t, ::fuchsia_net_tun::wire::DeviceReadFrameResponse*>&
        read_frame_result = read_frame_wire_result.value();

    if (read_frame_result.is_error()) {
      GTEST_FAIL() << "ReadFrame failed: " << zx_status_get_string(read_frame_result.error_value());
    } else {
      ASSERT_EQ(read_frame_result.value()->frame.frame_type(),
                fuchsia_hardware_network::wire::FrameType::kEthernet);
      ASSERT_EQ(read_frame_result.value()->frame.port(), kDefaultTestPort);
      ASSERT_NO_FATAL_FAILURE(
          SimpleClient::ValidateData(read_frame_result.value()->frame.data(), 0x00));
      ASSERT_FALSE(read_frame_result.value()->frame.has_meta());
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
    frame.set_frame_type(fuchsia_hardware_network::wire::FrameType::kEthernet);
    frame.set_port(kDefaultTestPort);
    uint8_t data[] = {0xAA, 0xBB};
    frame.set_data(alloc_, fidl::VectorView<uint8_t>::FromExternal(data));
    fidl::WireResult write_frame_wire_result = tun->WriteFrame(std::move(frame));
    ASSERT_OK(write_frame_wire_result.status());

    const fit::result<int32_t>& write_frame_result = write_frame_wire_result.value();
    if (write_frame_result.is_error()) {
      ASSERT_STATUS(write_frame_result.error_value(), ZX_ERR_SHOULD_WAIT);
      ASSERT_STATUS(
          signals.wait_one(static_cast<uint32_t>(fuchsia_net_tun::wire::Signals::kWritable),
                           zx::time::infinite_past(), nullptr),
          ZX_ERR_TIMED_OUT);
    } else {
      GTEST_FAIL() << "WriteFrame succeeded unexpectedly";
    }
  }

  ASSERT_OK(client.SendRx({0x02}, true));

  // But if we sent stuff out, now it should work after waiting for the available signal.
  ASSERT_OK(signals.wait_one(static_cast<uint32_t>(fuchsia_net_tun::wire::Signals::kWritable),
                             zx::deadline_after(kTimeout), nullptr));
  {
    fuchsia_net_tun::wire::Frame frame(alloc_);
    frame.set_frame_type(fuchsia_hardware_network::wire::FrameType::kEthernet);
    frame.set_port(kDefaultTestPort);
    uint8_t data[] = {0xAA, 0xBB};
    frame.set_data(alloc_, fidl::VectorView<uint8_t>::FromExternal(data));
    fidl::WireResult write_frame_wire_result = tun->WriteFrame(std::move(frame));
    ASSERT_OK(write_frame_wire_result.status());

    const fit::result<int32_t>& write_frame_result = write_frame_wire_result.value();
    if (write_frame_result.is_error()) {
      GTEST_FAIL() << "WriteFrame failed: "
                   << zx_status_get_string(write_frame_result.error_value());
    }
  }

  // Check that data was correctly written to descriptor.
  ASSERT_OK(client.FetchRx(&desc));
  ASSERT_EQ(desc, 0x02);
  auto* d = client.descriptor(desc);
  EXPECT_EQ(d->data_length, 2u);
  EXPECT_EQ(d->port_id.base, port_id.base);
  EXPECT_EQ(d->port_id.salt, port_id.salt);
  auto data = client.data(d);
  EXPECT_EQ(data[0], 0xAA);
  EXPECT_EQ(data[1], 0xBB);
}

TEST_F(TunTest, PairRxTx) {
  SimpleClient left, right;

  zx::result left_request = left.NewRequest();
  ASSERT_OK(left_request.status_value());
  zx::result right_request = right.NewRequest();
  ASSERT_OK(right_request.status_value());

  zx::result client_end =
      CreatePairAndPort(DefaultDevicePairConfig(), DefaultDevicePairPortConfig());
  ASSERT_OK(client_end.status_value());
  fidl::WireSyncClient device_pair{std::move(client_end.value())};
  ASSERT_OK(device_pair->GetLeft(std::move(left_request.value())).status());
  ASSERT_OK(device_pair->GetRight(std::move(right_request.value())).status());

  zx::result maybe_port_id = GetPairPortIds(kDefaultTestPort, device_pair.client_end());
  ASSERT_OK(maybe_port_id.status_value());
  const auto [left_port_id, right_port_id] = maybe_port_id.value();

  ASSERT_OK(left.OpenSession());
  ASSERT_OK(right.OpenSession());

  ASSERT_OK(right.SendRx({0x05, 0x06, 0x07}, true));
  ASSERT_OK(right.AttachPort(right_port_id));
  ASSERT_OK(left.AttachPort(left_port_id));
  ASSERT_OK(right.WaitOnline(right_port_id));
  ASSERT_OK(left.WaitOnline(left_port_id));

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

  zx::result left_request = left.NewRequest();
  ASSERT_OK(left_request.status_value());
  zx::result right_request = right.NewRequest();
  ASSERT_OK(right_request.status_value());

  zx::result client_end =
      CreatePairAndPort(DefaultDevicePairConfig(), DefaultDevicePairPortConfig());
  ASSERT_OK(client_end.status_value());
  fidl::WireSyncClient device_pair{std::move(client_end.value())};
  ASSERT_OK(device_pair->GetLeft(std::move(left_request.value())).status());
  ASSERT_OK(device_pair->GetRight(std::move(right_request.value())).status());

  zx::result maybe_port_id = GetPairPortIds(kDefaultTestPort, device_pair.client_end());
  ASSERT_OK(maybe_port_id.status_value());
  const auto [left_port_id, right_port_id] = maybe_port_id.value();

  ASSERT_OK(left.OpenSession());
  ASSERT_OK(right.OpenSession());

  constexpr uint32_t kWatcherBufferLength = 2;

  // Online status should be false for both sides before a session is opened.
  zx::result left_watcher_status =
      GetStatusWatcher(left.device().client_end(), left_port_id, kWatcherBufferLength);
  ASSERT_OK(left_watcher_status.status_value());
  fidl::WireSyncClient left_watcher{std::move(left_watcher_status.value())};
  zx::result right_watcher_status =
      GetStatusWatcher(right.device().client_end(), right_port_id, kWatcherBufferLength);
  ASSERT_OK(right_watcher_status.status_value());
  fidl::WireSyncClient right_watcher =
      fidl::WireSyncClient(std::move(right_watcher_status.value()));

  {
    fidl::WireResult left_watch_status_result = left_watcher->WatchStatus();
    ASSERT_OK(left_watch_status_result.status());
    fuchsia_hardware_network::wire::PortStatus status_left =
        left_watch_status_result.value().port_status;
    EXPECT_EQ(status_left.flags() & fuchsia_hardware_network::wire::StatusFlags::kOnline,
              fuchsia_hardware_network::wire::StatusFlags());

    fidl::WireResult right_watch_status_result = right_watcher->WatchStatus();
    ASSERT_OK(right_watch_status_result.status());
    fuchsia_hardware_network::wire::PortStatus status_right =
        right_watch_status_result.value().port_status;
    EXPECT_EQ(status_right.flags() & fuchsia_hardware_network::wire::StatusFlags::kOnline,
              fuchsia_hardware_network::wire::StatusFlags());
  }

  // When both sessions are unpaused, online signal must come up.
  ASSERT_OK(left.AttachPort(left_port_id));
  ASSERT_OK(right.AttachPort(right_port_id));

  {
    fidl::WireResult left_watch_status_result = left_watcher->WatchStatus();
    ASSERT_OK(left_watch_status_result.status());
    fuchsia_hardware_network::wire::PortStatus status_left =
        left_watch_status_result.value().port_status;
    EXPECT_EQ(status_left.flags() & fuchsia_hardware_network::wire::StatusFlags::kOnline,
              fuchsia_hardware_network::wire::StatusFlags::kOnline);

    fidl::WireResult right_watch_status_result = right_watcher->WatchStatus();
    ASSERT_OK(right_watch_status_result.status());
    fuchsia_hardware_network::wire::PortStatus status_right =
        right_watch_status_result.value().port_status;
    EXPECT_EQ(status_right.flags() & fuchsia_hardware_network::wire::StatusFlags::kOnline,
              fuchsia_hardware_network::wire::StatusFlags::kOnline);
  }
}

TEST_F(TunTest, PairFallibleWrites) {
  SimpleClient left, right;

  zx::result left_request = left.NewRequest();
  ASSERT_OK(left_request.status_value());
  zx::result right_request = right.NewRequest();
  ASSERT_OK(right_request.status_value());

  fuchsia_net_tun::wire::DevicePairConfig config = DefaultDevicePairConfig();
  config.set_fallible_transmit_left(true);

  zx::result client_end = CreatePairAndPort(std::move(config), DefaultDevicePairPortConfig());
  ASSERT_OK(client_end.status_value());
  fidl::WireSyncClient device_pair{std::move(client_end.value())};
  ASSERT_OK(device_pair->GetLeft(std::move(left_request.value())).status());
  ASSERT_OK(device_pair->GetRight(std::move(right_request.value())).status());

  zx::result maybe_port_id = GetPairPortIds(kDefaultTestPort, device_pair.client_end());
  ASSERT_OK(maybe_port_id.status_value());
  const auto [left_port_id, right_port_id] = maybe_port_id.value();

  ASSERT_OK(left.OpenSession());
  ASSERT_OK(right.OpenSession());
  ASSERT_OK(left.AttachPort(left_port_id));
  ASSERT_OK(right.AttachPort(right_port_id));
  ASSERT_OK(left.WaitOnline(left_port_id));
  ASSERT_OK(right.WaitOnline(right_port_id));

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

  zx::result left_request = left.NewRequest();
  ASSERT_OK(left_request.status_value());
  zx::result right_request = right.NewRequest();
  ASSERT_OK(right_request.status_value());

  zx::result client_end =
      CreatePairAndPort(DefaultDevicePairConfig(), DefaultDevicePairPortConfig());
  ASSERT_OK(client_end.status_value());
  fidl::WireSyncClient device_pair{std::move(client_end.value())};
  ASSERT_OK(device_pair->GetLeft(std::move(left_request.value())).status());
  ASSERT_OK(device_pair->GetRight(std::move(right_request.value())).status());

  zx::result maybe_port_id = GetPairPortIds(kDefaultTestPort, device_pair.client_end());
  ASSERT_OK(maybe_port_id.status_value());
  const auto [left_port_id, right_port_id] = maybe_port_id.value();

  ASSERT_OK(left.OpenSession());
  ASSERT_OK(right.OpenSession());
  ASSERT_OK(left.AttachPort(left_port_id));
  ASSERT_OK(right.AttachPort(right_port_id));
  ASSERT_OK(left.WaitOnline(left_port_id));
  ASSERT_OK(right.WaitOnline(right_port_id));

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

TEST_F(TunTest, RejectsMissingFrameFields) {
  zx::result device_and_port =
      CreateDeviceAndPort(DefaultDeviceConfig(), DefaultDevicePortConfig());

  ASSERT_OK(device_and_port.status_value());
  auto& [device_client_end, port_client_end] = *device_and_port;
  fidl::WireSyncClient tun{std::move(device_client_end)};
  fidl::WireSyncClient tun_port{std::move(port_client_end)};

  std::vector<uint8_t> empty_vec;

  const struct {
    const char* name;
    fit::function<void(fuchsia_net_tun::wire::Frame&)> update_frame;
    zx_status_t expect;
  } kTests[] = {
      {
          .name = "baseline",
          .update_frame = [](fuchsia_net_tun::wire::Frame& frame) {},
          // Baseline, port is offline.
          .expect = ZX_ERR_BAD_STATE,
      },
      {
          .name = "no frame type",
          .update_frame = [](fuchsia_net_tun::wire::Frame& frame) { frame.clear_frame_type(); },
          .expect = ZX_ERR_INVALID_ARGS,
      },
      {
          .name = "no data",
          .update_frame = [](fuchsia_net_tun::wire::Frame& frame) { frame.clear_data(); },
          .expect = ZX_ERR_INVALID_ARGS,
      },
      {
          .name = "empty data",
          .update_frame =
              [this, &empty_vec](fuchsia_net_tun::wire::Frame& frame) {
                frame.set_data(alloc_, fidl::VectorView<uint8_t>::FromExternal(empty_vec));
              },
          .expect = ZX_ERR_INVALID_ARGS,
      },
      {
          .name = "no port ID",
          .update_frame = [](fuchsia_net_tun::wire::Frame& frame) { frame.clear_port(); },
          .expect = ZX_ERR_INVALID_ARGS,
      },
      {
          .name = "invalid port ID",
          .update_frame =
              [](fuchsia_net_tun::wire::Frame& frame) {
                frame.set_port(fuchsia_hardware_network::wire::kMaxPorts);
              },
          .expect = ZX_ERR_INVALID_ARGS,
      },
  };

  for (const auto& test : kTests) {
    SCOPED_TRACE(test.name);
    // Build a valid frame then let each test case update it to make it invalid.
    fuchsia_net_tun::wire::Frame frame(alloc_);
    frame.set_frame_type(fuchsia_hardware_network::wire::FrameType::kEthernet);
    uint8_t data[] = {0x01, 0x02, 0x03};
    frame.set_data(alloc_, fidl::VectorView<uint8_t>::FromExternal(data));
    frame.set_port(kDefaultTestPort);

    test.update_frame(frame);

    fidl::WireResult write_frame_wire_result = tun->WriteFrame(std::move(frame));
    ASSERT_OK(write_frame_wire_result.status());

    const fit::result<int32_t>& write_frame_result = write_frame_wire_result.value();
    if (write_frame_result.is_error()) {
      ASSERT_STATUS(write_frame_result.error_value(), test.expect);
    } else {
      GTEST_FAIL() << "WriteFrame succeeded unexpectedly";
    }
  }
}

TEST_F(TunTest, RejectsIfOffline) {
  zx::result device_and_port =
      CreateDeviceAndPort(DefaultDeviceConfig(), DefaultDevicePortConfig());
  ASSERT_OK(device_and_port.status_value());
  auto& [device_client_end, port_client_end] = *device_and_port;

  zx::result maybe_port_id = GetPortId(port_client_end);
  ASSERT_OK(maybe_port_id.status_value());
  const fuchsia_hardware_network::wire::PortId port_id = maybe_port_id.value();

  fidl::WireSyncClient tun{std::move(device_client_end)};
  fidl::WireSyncClient tun_port{std::move(port_client_end)};

  SimpleClient client;
  zx::result request = client.NewRequest();
  ASSERT_OK(request.status_value());
  ASSERT_OK(tun->GetDevice(std::move(request.value())).status());
  ASSERT_OK(client.OpenSession());
  ASSERT_OK(client.AttachPort(port_id));

  // Can't send from the tun end.
  {
    fuchsia_net_tun::wire::Frame frame(alloc_);
    frame.set_frame_type(fuchsia_hardware_network::wire::FrameType::kEthernet);
    uint8_t data[] = {0x01, 0x02, 0x03};
    frame.set_data(alloc_, fidl::VectorView<uint8_t>::FromExternal(data));
    frame.set_port(kDefaultTestPort);
    fidl::WireResult write_frame_wire_result = tun->WriteFrame(std::move(frame));
    ASSERT_OK(write_frame_wire_result.status());

    const fit::result<int32_t>& write_frame_result = write_frame_wire_result.value();
    if (write_frame_result.is_error()) {
      ASSERT_STATUS(write_frame_result.error_value(), ZX_ERR_BAD_STATE);
    } else {
      GTEST_FAIL() << "WriteFrame succeeded unexpectedly";
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
  ASSERT_OK(tun_port->SetOnline(true).status());

  // Send from client end once more and read a single frame.
  {
    ASSERT_OK(client.SendTx({0x00, 0x01}, true));

    fidl::WireResult read_frame_wire_result = tun->ReadFrame();
    ASSERT_OK(read_frame_wire_result.status());
    const fit::result<int32_t, ::fuchsia_net_tun::wire::DeviceReadFrameResponse*>&
        read_frame_result = read_frame_wire_result.value();
    if (read_frame_result.is_error()) {
      GTEST_FAIL() << "ReadFrame failed: " << zx_status_get_string(read_frame_result.error_value());
    }
    ASSERT_EQ(read_frame_result.value()->frame.frame_type(),
              fuchsia_hardware_network::wire::FrameType::kEthernet);
    ASSERT_NO_FATAL_FAILURE(
        SimpleClient::ValidateData(read_frame_result.value()->frame.data(), 0x00));
    ASSERT_FALSE(read_frame_result.value()->frame.has_meta());
  }
  // Set offline and see if client received their tx buffers back.
  ASSERT_OK(tun_port->SetOnline(false).status());

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

  zx::result left_request = left.NewRequest();
  ASSERT_OK(left_request.status_value());
  zx::result right_request = right.NewRequest();
  ASSERT_OK(right_request.status_value());

  zx::result client_end =
      CreatePairAndPort(DefaultDevicePairConfig(), DefaultDevicePairPortConfig());
  ASSERT_OK(client_end.status_value());
  fidl::WireSyncClient device_pair{std::move(client_end.value())};
  ASSERT_OK(device_pair->GetLeft(std::move(left_request.value())).status());
  ASSERT_OK(device_pair->GetRight(std::move(right_request.value())).status());

  zx::result maybe_port_id = GetPairPortIds(kDefaultTestPort, device_pair.client_end());
  ASSERT_OK(maybe_port_id.status_value());
  const auto [left_port_id, right_port_id] = maybe_port_id.value();

  ASSERT_OK(left.OpenSession());
  ASSERT_OK(right.OpenSession());
  ASSERT_OK(left.AttachPort(left_port_id));
  ASSERT_OK(right.AttachPort(right_port_id));
  ASSERT_OK(left.WaitOnline(left_port_id));
  ASSERT_OK(right.WaitOnline(right_port_id));

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
    ASSERT_OK(right.FetchRx(rx_buffer.data(), &count));
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
    ASSERT_OK(left.FetchRx(rx_buffer.data(), &count));
    for (size_t i = 0; i < count; i++) {
      auto orig_desc = tx_descriptors[received + i];
      ASSERT_NO_FATAL_FAILURE(left.ValidateDataInPlace(rx_buffer[i], orig_desc, 4));
    }
    received += count;
  }
}

TEST_F(TunTest, ReportsInternalTxErrors) {
  fuchsia_net_tun::wire::DeviceConfig device_config = DefaultDeviceConfig();
  fuchsia_net_tun::wire::DevicePortConfig port_config = DefaultDevicePortConfig();
  port_config.set_online(true);
  // We need tun to be nonblocking so we're able to excite the path that attempts to copy a tx
  // buffer into FIDL and fails because we've removed the VMOs. If the call was blocking it'd block
  // forever and we wouldn't be able to use a sync client here.
  device_config.set_blocking(false);
  zx::result device_and_port =
      CreateDeviceAndPort(std::move(device_config), std::move(port_config));
  ASSERT_OK(device_and_port.status_value());
  auto& [device_client_end, port_client_end] = *device_and_port;

  zx::result maybe_port_id = GetPortId(port_client_end);
  ASSERT_OK(maybe_port_id.status_value());
  const fuchsia_hardware_network::wire::PortId port_id = maybe_port_id.value();

  fidl::WireSyncClient tun{std::move(device_client_end)};
  fidl::WireSyncClient tun_port{std::move(port_client_end)};

  SimpleClient client;
  zx::result request = client.NewRequest();
  ASSERT_OK(request.status_value());
  ASSERT_OK(tun->GetDevice(std::move(request.value())).status());
  ASSERT_OK(client.OpenSession());
  ASSERT_OK(client.AttachPort(port_id));

  // Wait for the device to observe the online session. This guarantees the Session's VMO will be
  // installed by the time we're done waiting.
  for (;;) {
    fidl::WireResult watch_state_result = tun_port->WatchState();
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
    fidl::WireResult read_frame_wire_result = tun->ReadFrame();
    ASSERT_OK(read_frame_wire_result.status());
    ASSERT_TRUE(read_frame_wire_result.value().is_error());
    ASSERT_STATUS(read_frame_wire_result.value().error_value(), ZX_ERR_SHOULD_WAIT);
  }
  const buffer_descriptor_t* desc = client.descriptor(descriptor);
  EXPECT_EQ(desc->return_flags,
            static_cast<uint32_t>(fuchsia_hardware_network::wire::TxReturnFlags::kTxRetError));
}

TEST_F(TunTest, ChainsRxBuffers) {
  constexpr uint32_t kRxBufferSize = 16;
  constexpr uint16_t kChainedBuffers = 3;
  fuchsia_net_tun::wire::DeviceConfig device_config = DefaultDeviceConfig();
  fuchsia_net_tun::wire::DevicePortConfig port_config = DefaultDevicePortConfig();
  port_config.set_online(true);
  fuchsia_net_tun::wire::BaseDeviceConfig base_device_config(alloc_);
  base_device_config.set_min_rx_buffer_length(kRxBufferSize);
  device_config.set_base(alloc_, std::move(base_device_config));

  zx::result device_and_port =
      CreateDeviceAndPort(std::move(device_config), std::move(port_config));
  ASSERT_OK(device_and_port.status_value());
  auto& [device_client_end, port_client_end] = *device_and_port;

  zx::result maybe_port_id = GetPortId(port_client_end);
  ASSERT_OK(maybe_port_id.status_value());
  const fuchsia_hardware_network::wire::PortId port_id = maybe_port_id.value();

  fidl::WireSyncClient tun{std::move(device_client_end)};

  SimpleClient client;
  zx::result request = client.NewRequest();
  ASSERT_OK(request.status_value());
  ASSERT_OK(tun->GetDevice(std::move(request.value())).status());
  ASSERT_OK(client.OpenSession());
  ASSERT_OK(client.AttachPort(port_id));

  for (uint16_t i = 0; i < kChainedBuffers; i++) {
    buffer_descriptor_t* desc = client.ResetDescriptor(i);
    desc->data_length = kRxBufferSize;
    ASSERT_OK(client.SendRx({i}, false));
  }

  std::vector<uint8_t> send_data;
  send_data.reserve(kRxBufferSize * kChainedBuffers);
  for (uint32_t i = 0; i < kRxBufferSize * kChainedBuffers; i++) {
    send_data.push_back(static_cast<uint8_t>(i));
  }

  fuchsia_net_tun::wire::Frame frame(alloc_);
  frame.set_port(kDefaultTestPort);
  frame.set_frame_type(fuchsia_hardware_network::wire::FrameType::kEthernet);
  frame.set_data(alloc_, fidl::VectorView<uint8_t>::FromExternal(send_data));
  fidl::WireResult write_frame_wire_result = tun->WriteFrame(std::move(frame));
  ASSERT_OK(write_frame_wire_result.status());
  ASSERT_TRUE(write_frame_wire_result->is_ok())
      << zx_status_get_string(write_frame_wire_result->error_value());

  uint16_t desc_idx;
  ASSERT_OK(client.FetchRx(&desc_idx));
  for (uint16_t i = 0; i < kChainedBuffers; i++) {
    SCOPED_TRACE(i);
    buffer_descriptor_t* desc = client.descriptor(desc_idx);
    ASSERT_EQ(desc->chain_length, kChainedBuffers - i - 1);
    ASSERT_EQ(desc->data_length, kRxBufferSize);
    ASSERT_EQ(memcmp(&send_data[kRxBufferSize * i], client.data(desc).begin(), kRxBufferSize), 0);
    desc_idx = desc->nxt;
  }
}

MATCHER(IsIdlePortEvent, "") {
  *result_listener << "which is " << arg.describe();
  return arg.which == fuchsia_hardware_network::wire::DevicePortEvent::Tag::kIdle;
}

MATCHER_P(IsAddedPortEvent, port, "") {
  *result_listener << "which is " << arg.describe();
  return arg.which == fuchsia_hardware_network::wire::DevicePortEvent::Tag::kAdded &&
         arg.port_id.value().base == port.base && arg.port_id.value().salt == port.salt;
}

MATCHER_P(IsRemovedPortEvent, port, "") {
  *result_listener << "which is " << arg.describe();
  return arg.which == fuchsia_hardware_network::wire::DevicePortEvent::Tag::kRemoved &&
         arg.port_id.value().base == port.base && arg.port_id.value().salt == port.salt;
}

TEST_F(TunTest, AddRemovePorts) {
  zx::result device_result = CreateDevice(DefaultDeviceConfig());
  ASSERT_OK(device_result.status_value());
  fidl::WireSyncClient tun{std::move(*device_result)};

  fidl::ClientEnd<fuchsia_hardware_network::Device> device;
  {
    zx::result server_end = fidl::CreateEndpoints(&device);
    ASSERT_OK(server_end.status_value());
    ASSERT_OK(tun->GetDevice(std::move(*server_end)).status());
  }
  zx::result port_watcher_result = GetPortWatcher(device);
  ASSERT_OK(port_watcher_result.status_value());
  fidl::WireSyncClient port_watcher{std::move(*port_watcher_result)};

  {
    zx::result event = WatchPorts(port_watcher);
    ASSERT_OK(event.status_value());
    ASSERT_THAT(event.value(), IsIdlePortEvent());
  }

  struct {
    const char* name;
    fuchsia_hardware_network::wire::PortId id;
    fidl::ClientEnd<fuchsia_net_tun::Port> client_end;
    bool sync;
  } ports[] = {
      {
          .name = "port 2",
          .id = {.base = 2},
          .sync = false,
      },
      {
          .name = "port 5",
          .id = {.base = 5},
          .sync = true,
      },
  };

  for (auto& port : ports) {
    SCOPED_TRACE(port.name);
    zx::result server_end = fidl::CreateEndpoints(&port.client_end);
    ASSERT_OK(server_end.status_value());
    fuchsia_net_tun::wire::DevicePortConfig port_config = DefaultDevicePortConfig();
    port_config.base().set_id(port.id.base);
    ASSERT_OK(tun->AddPort(std::move(port_config), std::move(*server_end)).status());

    zx::result port_id = GetPortId(port.client_end);
    ASSERT_OK(port_id.status_value());
    ASSERT_EQ(port_id.value().base, port.id.base);
    port.id = port_id.value();

    zx::result event = WatchPorts(port_watcher);
    ASSERT_OK(event.status_value());
    ASSERT_THAT(event.value(), IsAddedPortEvent(port.id));
  }

  // Adding the same port again returns the appropriate error.
  {
    fidl::ClientEnd<fuchsia_net_tun::Port> client_end;
    zx::result server_end = fidl::CreateEndpoints(&client_end);
    ASSERT_OK(server_end.status_value());
    fuchsia_net_tun::wire::DevicePortConfig port_config = DefaultDevicePortConfig();
    port_config.base().set_id(ports[0].id.base);
    ASSERT_OK(tun->AddPort(std::move(port_config), std::move(*server_end)).status());

    CapturingEventHandler<fuchsia_net_tun::Port> handler;
    fidl::WireClient client(std::move(client_end), dispatcher(), &handler);

    ASSERT_TRUE(
        RunLoopWithTimeoutOrUntil([&handler] { return handler.info_.has_value(); }, kTimeout));
    ASSERT_EQ(handler.info_.value().status(), ZX_ERR_ALREADY_EXISTS);
  }

  for (auto& port : ports) {
    SCOPED_TRACE(port.name);
    if (port.sync) {
      ASSERT_OK(fidl::WireCall(port.client_end)->Remove().status());
      ASSERT_OK(port.client_end.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(),
                                                   nullptr));
    } else {
      port.client_end.reset();
    }

    zx::result event = WatchPorts(port_watcher);
    ASSERT_OK(event.status_value());
    ASSERT_THAT(event.value(), IsRemovedPortEvent(port.id));
  }
}

TEST_F(TunTest, AddRemovePairPorts) {
  zx::result pair_result = CreatePair(DefaultDevicePairConfig());
  ASSERT_OK(pair_result.status_value());
  fidl::WireSyncClient tun{std::move(*pair_result)};

  struct {
    const char* name;
    bool left;
    fidl::ClientEnd<fuchsia_hardware_network::Device> device;
    fidl::WireSyncClient<fuchsia_hardware_network::PortWatcher> watcher;
  } ends[] = {
      {.name = "left", .left = true},
      {.name = "right", .left = false},
  };
  for (auto& end : ends) {
    SCOPED_TRACE(end.name);
    zx::result server_end = fidl::CreateEndpoints(&end.device);
    ASSERT_OK(server_end.status_value());
    if (end.left) {
      ASSERT_OK(tun->GetLeft(std::move(*server_end)).status());
    } else {
      ASSERT_OK(tun->GetRight(std::move(*server_end)).status());
    }
    zx::result port_watcher_result = GetPortWatcher(end.device);
    ASSERT_OK(port_watcher_result.status_value());
    end.watcher = fidl::WireSyncClient(std::move(*port_watcher_result));

    zx::result event = WatchPorts(end.watcher);
    ASSERT_OK(event.status_value());

    ASSERT_THAT(event.value(), IsIdlePortEvent());
  }

  struct {
    const char* name;
    uint8_t base_id;
    fuchsia_hardware_network::wire::PortId left_id;
    fuchsia_hardware_network::wire::PortId right_id;
  } ports[] = {
      {
          .name = "port 2",
          .base_id = 2,
      },
      {
          .name = "port 5",
          .base_id = 5,
      },
  };

  for (auto& port : ports) {
    SCOPED_TRACE(port.name);
    fuchsia_net_tun::wire::DevicePairPortConfig port_config = DefaultDevicePairPortConfig();
    port_config.base().set_id(port.base_id);
    fidl::WireResult result = tun->AddPort(std::move(port_config));
    ASSERT_OK(result.status());
    ASSERT_TRUE(result->is_ok()) << zx_status_get_string(result->error_value());

    zx::result port_id = GetPairPortIds(port.base_id, tun.client_end());
    ASSERT_OK(port_id.status_value());
    const auto [left_id, right_id] = port_id.value();
    ASSERT_EQ(left_id.base, port.base_id);
    port.left_id = left_id;
    ASSERT_EQ(right_id.base, port.base_id);
    port.right_id = right_id;

    for (auto& end : ends) {
      SCOPED_TRACE(end.name);
      zx::result event = WatchPorts(end.watcher);
      ASSERT_OK(event.status_value());
      const fuchsia_hardware_network::wire::PortId port_id = [&port, &end]() {
        if (end.left) {
          return port.left_id;
        }
        return port.right_id;
      }();
      ASSERT_THAT(event.value(), IsAddedPortEvent(port_id));
    }
  }

  // Adding the same port again returns the appropriate error.
  {
    fuchsia_net_tun::wire::DevicePairPortConfig port_config = DefaultDevicePairPortConfig();
    port_config.base().set_id(ports[0].base_id);
    fidl::WireResult result = tun->AddPort(std::move(port_config));
    ASSERT_OK(result.status());
    ASSERT_TRUE(result->is_error());
    ASSERT_EQ(result->error_value(), ZX_ERR_ALREADY_EXISTS);
  }

  for (auto& port : ports) {
    SCOPED_TRACE(port.name);

    fidl::WireResult result = tun->RemovePort(port.base_id);
    ASSERT_OK(result.status());
    ASSERT_TRUE(result->is_ok()) << zx_status_get_string(result->error_value());

    for (auto& end : ends) {
      SCOPED_TRACE(end.name);
      zx::result event = WatchPorts(end.watcher);
      ASSERT_OK(event.status_value());
      const fuchsia_hardware_network::wire::PortId port_id = [&port, &end]() {
        if (end.left) {
          return port.left_id;
        }
        return port.right_id;
      }();
      ASSERT_THAT(event.value(), IsRemovedPortEvent(port_id));
    }
  }

  // Removing a port that doesn't exist anymore returns the appropriate error.
  fidl::WireResult result = tun->RemovePort(ports[0].base_id);
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->is_error());
  ASSERT_EQ(result->error_value(), ZX_ERR_NOT_FOUND);
}

}  // namespace testing
}  // namespace tun
}  // namespace network
