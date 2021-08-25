// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_netstack.h"

#include <lib/async/default.h>
#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <netinet/icmp6.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <zircon/device/ethernet.h>

static constexpr size_t kMtu = 1500;
static constexpr size_t kVmoSize = kMtu * 2;

static constexpr uint8_t kHostMacAddress[ETH_ALEN] = {0x02, 0x1a, 0x11, 0x00, 0x00, 0x00};

static constexpr uint8_t kHostIpv4Address[4] = {192, 168, 0, 1};
static constexpr uint8_t kGuestIpv4Address[4] = {192, 168, 0, 10};

static constexpr uint16_t kProtocolIpv4 = 0x0800;
static constexpr uint8_t kPacketTypeUdp = 17;
static constexpr uint16_t kTestPort = 4242;

zx_status_t Device::Create(fuchsia::hardware::ethernet::DeviceSyncPtr eth_device,
                           std::unique_ptr<Device>* out) {
  std::unique_ptr<fuchsia::hardware::ethernet::Fifos> fifos;
  zx_status_t status;
  eth_device->GetFifos(&status, &fifos);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to get fifos: " << status;
    return status;
  }

  zx::vmo vmo;
  status = zx::vmo::create(kVmoSize, 0, &vmo);
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

  uintptr_t io_addr;
  status =
      zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_REQUIRE_NON_RESIZABLE,
                                 0, vmo, 0, kVmoSize, &io_addr);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to map vmo: " << status;
    return status;
  }

  eth_fifo_entry_t entry;
  entry.offset = 0;
  entry.length = kMtu;
  entry.flags = 0;
  entry.cookie = 0;
  status = fifos->rx.write(sizeof(eth_fifo_entry_t), &entry, 1, nullptr);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to write to rx fifo: " << status;
    return status;
  }

  eth_device->Start(&status);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to start ethernet device: " << status;
    return status;
  }

  *out = std::unique_ptr<Device>(new Device(std::move(eth_device), std::move(fifos->rx),
                                            std::move(fifos->tx), std::move(vmo), io_addr));
  return ZX_OK;
}

zx_status_t Device::Start(async_dispatcher_t* dispatcher) {
  zx_status_t status = rx_wait_.Begin(dispatcher);
  if (status != ZX_OK) {
    return status;
  }
  return tx_wait_.Begin(dispatcher);
}

void Device::OnReceive(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                       const zx_packet_signal_t* signal) {
  if (status == ZX_ERR_CANCELED) {
    return;
  } else if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Device receive waiter failed " << status;
    return;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (signal->observed & ZX_SOCKET_PEER_CLOSED) {
      while (!rx_completers_.empty()) {
        rx_completers_.back().complete_error(ZX_ERR_PEER_CLOSED);
        rx_completers_.pop_back();
      }
      return;
    }

    while (!rx_completers_.empty()) {
      eth_fifo_entry_t entry;
      zx_status_t status = rx_.read(sizeof(eth_fifo_entry_t), &entry, 1, nullptr);
      if (status == ZX_ERR_SHOULD_WAIT) {
        break;
      }

      if (status != ZX_OK) {
        rx_completers_.back().complete_error(status);
        rx_completers_.pop_back();
        break;
        ;
      }
      rx_completers_.back().complete_ok(std::move(entry));
      rx_completers_.pop_back();
    }
  }

  status = wait->Begin(dispatcher);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to wait for device rx fifo " << status;
  }
}

void Device::OnTransmit(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                        const zx_packet_signal_t* signal) {
  if (status == ZX_ERR_CANCELED) {
    return;
  } else if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Device receive waiter failed " << status;
    return;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (signal->observed & ZX_SOCKET_PEER_CLOSED) {
      while (!tx_completers_.empty()) {
        tx_completers_.back().complete_error(ZX_ERR_PEER_CLOSED);
        tx_completers_.pop_back();
      }
      return;
    }

    while (!tx_completers_.empty()) {
      eth_fifo_entry_t entry;
      zx_status_t status = tx_.read(sizeof(eth_fifo_entry_t), &entry, 1, nullptr);
      if (status == ZX_ERR_SHOULD_WAIT) {
        break;
      }

      if (status != ZX_OK) {
        tx_completers_.back().complete_error(status);
        tx_completers_.pop_back();
        break;
        ;
      }
      tx_completers_.back().complete_ok(std::move(entry));
      tx_completers_.pop_back();
    }
  }

  status = wait->Begin(dispatcher);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to wait for device tx fifo " << status;
  }
}

fpromise::promise<eth_fifo_entry_t, zx_status_t> Device::GetRxEntry() {
  fpromise::bridge<eth_fifo_entry_t, zx_status_t> bridge;
  std::lock_guard<std::mutex> lock(mutex_);
  rx_completers_.push_back(std::move(bridge.completer));
  return bridge.consumer.promise();
}

fpromise::promise<eth_fifo_entry_t, zx_status_t> Device::GetTxEntry() {
  fpromise::bridge<eth_fifo_entry_t, zx_status_t> bridge;
  std::lock_guard<std::mutex> lock(mutex_);
  tx_completers_.push_back(std::move(bridge.completer));
  return bridge.consumer.promise();
}

fpromise::promise<std::vector<uint8_t>, zx_status_t> Device::ReadPacket() {
  return GetRxEntry().and_then(
      [this](const eth_fifo_entry_t& entry) -> fpromise::result<std::vector<uint8_t>, zx_status_t> {
        if (entry.flags != ETH_FIFO_RX_OK) {
          return fpromise::error(ZX_ERR_IO);
        }
        if (entry.length > kMtu) {
          return fpromise::error(ZX_ERR_INTERNAL);
        }
        std::vector<uint8_t> packet(entry.length);
        memcpy(packet.data(), reinterpret_cast<void*>(io_addr_ + entry.offset), packet.size());

        memset(reinterpret_cast<void*>(io_addr_), 0, kMtu);
        eth_fifo_entry_t new_entry;
        new_entry.offset = 0;
        new_entry.length = kMtu;
        new_entry.flags = 0;
        new_entry.cookie = 0;
        zx_status_t status = rx_.write(sizeof(eth_fifo_entry_t), &new_entry, 1, nullptr);
        if (status != ZX_OK) {
          FX_LOGS(ERROR) << "Failed to write to rx fifo: " << status;
          return fpromise::error(status);
        }

        return fpromise::ok(std::move(packet));
      });
}

fpromise::promise<void, zx_status_t> Device::WritePacket(std::vector<uint8_t> packet) {
  eth_fifo_entry_t entry = {
      .offset = kMtu,
      .length = static_cast<uint16_t>(packet.size()),
  };

  memcpy(reinterpret_cast<void*>(io_addr_ + entry.offset), packet.data(), packet.size());

  size_t count;
  zx_status_t status = tx_.write(sizeof(eth_fifo_entry_t), &entry, 1, &count);
  if (status != ZX_OK) {
    return fpromise::make_error_promise(status);
  }
  if (count != 1) {
    return fpromise::make_error_promise(ZX_ERR_INTERNAL);
  }

  return GetTxEntry().and_then(
      [](const eth_fifo_entry_t& entry) -> fpromise::result<void, zx_status_t> {
        if (entry.flags != ETH_FIFO_TX_OK) {
          return fpromise::error(ZX_ERR_IO);
        }
        return fpromise::ok();
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
  status = Device::Create(std::move(device_sync_ptr), &device);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create device " << status;
    return;
  }

  status = device->Start(loop_.dispatcher());
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
    return fpromise::make_promise([device = &itr->second] { return fpromise::ok(device->get()); });
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
