// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_netstack.h"

#include <lib/async/default.h>
#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <zircon/device/ethernet.h>

static constexpr uint32_t kMtu = 1500;

static constexpr uint8_t kHostMacAddress[ETH_ALEN] = {0x02, 0x1a, 0x11, 0x00, 0x00, 0x00};

static constexpr uint8_t kHostIpv4Address[4] = {192, 168, 0, 1};
static constexpr uint8_t kGuestIpv4Address[4] = {192, 168, 0, 10};

static constexpr uint16_t kProtocolIpv4 = 0x0800;
static constexpr uint8_t kPacketTypeUdp = 17;
static constexpr uint16_t kTestPort = 4242;

zx_status_t Device::Create(async_dispatcher_t* dispatcher,
                           fuchsia::hardware::ethernet::DeviceSyncPtr eth_device,
                           std::unique_ptr<Device>* out) {
  std::unique_ptr<fuchsia::hardware::ethernet::Fifos> fifos;
  zx_status_t status;
  eth_device->GetFifos(&status, &fifos);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to get fifos: " << status;
    return status;
  }

  const uint32_t rx_storage = 2 * fifos->rx_depth;
  const uint32_t tx_storage = 2 * fifos->tx_depth;

  uint32_t offset = 0;

  std::vector<eth_fifo_entry_t> rx_entries;
  rx_entries.reserve(rx_storage);
  for (size_t i = 0; i < rx_storage; ++i) {
    rx_entries.emplace_back(eth_fifo_entry_t{
        .offset = offset,
        .length = kMtu,
    });
    offset += kMtu;
  }

  std::vector<eth_fifo_entry_t> tx_entries;
  tx_entries.reserve(tx_storage);
  for (size_t i = 0; i < tx_storage; ++i) {
    tx_entries.emplace_back(eth_fifo_entry_t{
        .offset = offset,
        .length = kMtu,
    });
    offset += kMtu;
  }

  const size_t vmoSize = offset;

  zx::vmo vmo;
  status = zx::vmo::create(vmoSize, 0, &vmo);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create vmo: " << status;
    return status;
  }

  zx::vmo vmo_dup;
  status = vmo.duplicate(ZX_RIGHTS_IO | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER, &vmo_dup);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to duplicate vmo: " << status;
    return status;
  }

  eth_device->SetIOBuffer(std::move(vmo_dup), &status);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to set IO buffer: " << status;
    return status;
  }

  uint8_t* io_addr;
  status =
      zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_REQUIRE_NON_RESIZABLE,
                                 0, vmo, 0, vmoSize, reinterpret_cast<uintptr_t*>(&io_addr));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to map vmo: " << status;
    return status;
  }

  *out = std::unique_ptr<Device>(new Device(dispatcher, std::move(eth_device), std::move(fifos->rx),
                                            std::move(rx_entries), std::move(fifos->tx),
                                            std::move(tx_entries), std::move(vmo), io_addr));

  return ZX_OK;
}

zx_status_t Device::Start() {
  zx_status_t status;
  eth_device_->Start(&status);
  if (status != ZX_OK) {
    return status;
  }
  async::WaitBase* waits[] = {
      &rx_.outbound_wait_,
      &rx_.inbound_wait_,
      &tx_.outbound_wait_,
      &tx_.inbound_wait_,
  };
  for (auto* wait : waits) {
    status = wait->Begin(dispatcher_);
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

void Device::FIFO::InboundHandler(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                  zx_status_t status, const zx_packet_signal_t* signal) {
  switch (status) {
    case ZX_OK:
      break;
    case ZX_ERR_CANCELED:
      return;
    default:
      FX_LOGS(ERROR) << "FIFO waiter failed " << status;
      return;
  }

  if (signal->observed & ZX_SOCKET_PEER_CLOSED) {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!completers_.empty()) {
      completers_.front().complete_error(ZX_ERR_PEER_CLOSED);
      completers_.pop();
    }
    return;
  }

  eth_fifo_entry_t entries[depth_];
  size_t actual;
  status = fifo_.read(sizeof(eth_fifo_entry_t), entries, depth_, &actual);
  switch (status) {
    case ZX_OK: {
      eth_fifo_entry_t* it = entries;
      std::lock_guard<std::mutex> lock(mutex_);
      while (!completers_.empty() && it != entries + actual) {
        completers_.front().complete_ok(*it++);
        completers_.pop();
      }
      std::copy(it, entries + actual, std::back_inserter(inbound_entries_));
      break;
    }
    case ZX_ERR_SHOULD_WAIT:
      break;
    default:
      std::lock_guard<std::mutex> lock(mutex_);
      while (!completers_.empty()) {
        completers_.front().complete_error(status);
        completers_.pop();
      }
  }

  status = wait->Begin(dispatcher);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to wait for device fifo " << status;
  }
}

void Device::FIFO::OutboundHandler(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                   zx_status_t status, const zx_packet_signal_t* signal) {
  switch (status) {
    case ZX_OK:
      break;
    case ZX_ERR_CANCELED:
      return;
    default:
      FX_LOGS(ERROR) << "FIFO waiter failed " << status;
      return;
  }

  if (signal->observed & ZX_SOCKET_PEER_CLOSED) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (outbound_entries_.empty()) {
    return;
  }

  size_t actual;
  status = fifo_.write(sizeof(eth_fifo_entry_t), outbound_entries_.data(), outbound_entries_.size(),
                       &actual);
  switch (status) {
    case ZX_OK: {
      outbound_entries_.erase(outbound_entries_.begin(), outbound_entries_.begin() + actual);
      break;
    }
    case ZX_ERR_SHOULD_WAIT:
      break;
    default:
      FX_LOGS(ERROR) << "FIFO write failed " << status;
      return;
  }

  if (outbound_entries_.empty()) {
    return;
  }

  status = wait->Begin(dispatcher);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to wait for device fifo " << status;
  }
}

fpromise::promise<eth_fifo_entry_t, zx_status_t> Device::FIFO::GetEntry() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!inbound_entries_.empty()) {
    eth_fifo_entry_t entry = inbound_entries_.back();
    inbound_entries_.pop_back();
    return fpromise::make_result_promise<eth_fifo_entry_t, zx_status_t>(fpromise::ok(entry));
  }
  fpromise::bridge<eth_fifo_entry_t, zx_status_t> bridge;
  completers_.push(std::move(bridge.completer));
  return bridge.consumer.promise();
}

fpromise::promise<std::vector<uint8_t>, zx_status_t> Device::ReadPacket() {
  return rx_.GetEntry().and_then(
      [this](eth_fifo_entry_t& entry) -> fpromise::result<std::vector<uint8_t>, zx_status_t> {
        if (entry.flags != ETH_FIFO_RX_OK) {
          return fpromise::error(ZX_ERR_IO);
        }
        if (entry.length > kMtu) {
          return fpromise::error(ZX_ERR_INTERNAL);
        }
        std::vector<uint8_t> packet;
        packet.reserve(entry.length);
        std::copy_n(io_addr_ + entry.offset, entry.length, std::back_inserter(packet));
        entry.length = kMtu;
        std::lock_guard<std::mutex> lock(rx_.mutex_);
        rx_.outbound_entries_.push_back(entry);
        zx_status_t status = rx_.outbound_wait_.Begin(dispatcher_);
        switch (status) {
          case ZX_OK:
          case ZX_ERR_ALREADY_EXISTS:
            return fpromise::ok(std::move(packet));
          default:
            return fpromise::error(status);
        }
      });
}

fpromise::promise<void, zx_status_t> Device::WritePacket(std::vector<uint8_t> packet) {
  if (packet.size() > kMtu) {
    return fpromise::make_error_promise(ZX_ERR_INTERNAL);
  }
  return tx_.GetEntry().and_then(
      [this,
       packet = std::move(packet)](eth_fifo_entry_t& entry) -> fpromise::result<void, zx_status_t> {
        std::copy(packet.begin(), packet.end(), io_addr_ + entry.offset);
        entry.length = static_cast<uint16_t>(packet.size());
        std::lock_guard<std::mutex> lock(tx_.mutex_);
        tx_.outbound_entries_.push_back(entry);
        zx_status_t status = tx_.outbound_wait_.Begin(dispatcher_);
        switch (status) {
          case ZX_OK:
          case ZX_ERR_ALREADY_EXISTS:
            return fpromise::ok();
          default:
            return fpromise::error(status);
        }
      });
}

void FakeState::NotImplemented_(const std::string& name) {
  FX_LOGS(ERROR) << "Not implemented: " << name;
}

void FakeNetstack::NotImplemented_(const std::string& name) {
  FX_LOGS(ERROR) << "Not implemented: " << name;
}

void FakeNetstack::BridgeInterfaces(std::vector<uint32_t> nicids,
                                    BridgeInterfacesCallback callback) {
  callback(fuchsia::netstack::NetErr{.status = fuchsia::netstack::Status::OK}, nic_counter_++);
}

void FakeNetstack::AddEthernetDevice(
    std::string topological_path, fuchsia::netstack::InterfaceConfig interfaceConfig,
    fidl::InterfaceHandle<::fuchsia::hardware::ethernet::Device> eth_device,
    AddEthernetDeviceCallback callback) {
  auto deferred = fit::defer([this, callback = std::move(callback)]() {
    callback(fuchsia::netstack::Netstack_AddEthernetDevice_Result::WithResponse(
        fuchsia::netstack::Netstack_AddEthernetDevice_Response{nic_counter_}));
    nic_counter_++;
  });

  auto device_sync_ptr = eth_device.BindSync();

  fuchsia::hardware::ethernet::Info device_info;
  zx_status_t status = device_sync_ptr->GetInfo(&device_info);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to get device info: " << status;
    return;
  }

  std::unique_ptr<Device> device;
  status = Device::Create(loop_.dispatcher(), std::move(device_sync_ptr), &device);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create device " << status;
    return;
  }

  status = device->Start();
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to start device " << status;
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto [itr, success] = devices_.insert(std::make_pair(device_info.mac, std::move(device)));
  if (!success) {
    FX_LOGS(ERROR) << "Device already exists";
    return;
  }

  auto completers_itr = completers_.find(device_info.mac);
  if (completers_itr == completers_.end()) {
    return;
  }
  while (!completers_itr->second.empty()) {
    completers_itr->second.back().complete_ok(itr->second.get());
    completers_itr->second.pop_back();
  }
}

void FakeNetstack::SetInterfaceStatus(uint32_t nicid, bool enabled) {
  // Ignored as our fake netstack does not track interface status.
}

static uint16_t checksum(const void* _data, size_t len, uint16_t _sum) {
  uint32_t sum = _sum;
  auto data = static_cast<const uint16_t*>(_data);
  for (; len > 1; len -= 2) {
    sum += *data++;
  }
  if (len) {
    sum += (*data & UINT8_MAX);
  }
  while (sum > UINT16_MAX) {
    sum = (sum & UINT16_MAX) + (sum >> 16);
  }
  return static_cast<uint16_t>(~sum);
}

fpromise::promise<void, zx_status_t> FakeNetstack::SendUdpPacket(
    const fuchsia::hardware::ethernet::MacAddress& mac_addr, std::vector<uint8_t> packet) {
  size_t total_length = sizeof(ethhdr) + sizeof(iphdr) + sizeof(udphdr) + packet.size();
  if (total_length > kMtu) {
    return fpromise::make_error_promise(ZX_ERR_BUFFER_TOO_SMALL);
  }

  std::vector<uint8_t> udp_packet;
  udp_packet.reserve(total_length);

  {
    ethhdr eth;
    static_assert(sizeof(eth.h_dest) == sizeof(mac_addr.octets));
    memcpy(eth.h_dest, mac_addr.octets.data(), sizeof(eth.h_dest));
    static_assert(sizeof(eth.h_source) == sizeof(kHostMacAddress));
    memcpy(eth.h_source, kHostMacAddress, sizeof(eth.h_source));
    eth.h_proto = htons(kProtocolIpv4);
    std::copy_n(reinterpret_cast<uint8_t*>(&eth), sizeof(eth), std::back_inserter(udp_packet));
  }

  {
    iphdr ip = {
        .version = 4,
        .tot_len = htons(static_cast<uint16_t>(sizeof(iphdr) + sizeof(udphdr) + packet.size())),
        .ttl = UINT8_MAX,
        .protocol = kPacketTypeUdp,
    };
    ip.ihl = sizeof(iphdr) >> 2;  // Header length in 32-bit words.
    static_assert(sizeof(ip.saddr) == sizeof(kHostIpv4Address));
    memcpy(&ip.saddr, kHostIpv4Address, sizeof(ip.saddr));
    static_assert(sizeof(ip.daddr) == sizeof(kGuestIpv4Address));
    memcpy(&ip.daddr, kGuestIpv4Address, sizeof(ip.daddr));
    ip.check = checksum(&ip, sizeof(iphdr), 0);
    std::copy_n(reinterpret_cast<uint8_t*>(&ip), sizeof(ip), std::back_inserter(udp_packet));
  }

  {
    udphdr udp = {
        .source = htons(kTestPort),
        .dest = htons(kTestPort),
        .len = htons(static_cast<uint16_t>(sizeof(udphdr) + packet.size())),
    };
    std::copy_n(reinterpret_cast<uint8_t*>(&udp), sizeof(udp), std::back_inserter(udp_packet));
  }

  std::copy(packet.begin(), packet.end(), std::back_inserter(udp_packet));

  return SendPacket(mac_addr, std::move(udp_packet));
}

fpromise::promise<Device*> FakeNetstack::GetDevice(
    const fuchsia::hardware::ethernet::MacAddress& mac_addr) {
  std::lock_guard<std::mutex> lock(mutex_);

  // If the device is already connected the the netstack then just return a pointer to it.
  auto itr = devices_.find(mac_addr);
  if (itr != devices_.end()) {
    return fpromise::make_result_promise(fpromise::ok(itr->second.get()));
  }

  // Otherwise, add to the list of completers for this MAC address. The promise will complete when
  // the devices calls AddEthernetDevice.
  fpromise::bridge<Device*> bridge;
  auto completers_itr = completers_.find(mac_addr);
  if (completers_itr == completers_.end()) {
    std::vector<fpromise::completer<Device*>> vec;
    vec.push_back(std::move(bridge.completer));
    completers_.insert(std::make_pair(mac_addr, std::move(vec)));
  } else {
    completers_itr->second.push_back(std::move(bridge.completer));
  }

  return bridge.consumer.promise();
}

fpromise::promise<void, zx_status_t> FakeNetstack::SendPacket(
    const fuchsia::hardware::ethernet::MacAddress& mac_addr, std::vector<uint8_t> packet) {
  if (packet.size() > kMtu) {
    return fpromise::make_error_promise(ZX_ERR_INVALID_ARGS);
  }

  return GetDevice(mac_addr).then(
      [packet = std::move(packet)](
          const fpromise::result<Device*>& result) mutable -> fpromise::promise<void, zx_status_t> {
        if (!result.is_ok()) {
          return fpromise::make_error_promise(ZX_ERR_INTERNAL);
        }
        Device* device = result.value();
        return device->WritePacket(std::move(packet));
      });
}

fpromise::promise<std::vector<uint8_t>, zx_status_t> FakeNetstack::ReceivePacket(
    const fuchsia::hardware::ethernet::MacAddress& mac_addr) {
  return GetDevice(mac_addr).then([](const fpromise::result<Device*>& result)
                                      -> fpromise::promise<std::vector<uint8_t>, zx_status_t> {
    if (!result.is_ok()) {
      return fpromise::make_result_promise<std::vector<uint8_t>, zx_status_t>(
          fpromise::error(ZX_ERR_INTERNAL));
    }
    Device* device = result.value();
    return device->ReadPacket();
  });
}
