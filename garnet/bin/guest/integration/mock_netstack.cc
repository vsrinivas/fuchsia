// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <netinet/icmp6.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>

#include <lib/fit/defer.h>
#include <src/lib/fxl/logging.h>
#include <zircon/device/ethernet.h>

#include "mock_netstack.h"

static constexpr size_t kMtu = 1500;
static constexpr size_t kVmoSize = kMtu * 2;

static constexpr uint8_t kHostMacAddress[ETH_ALEN] = {0x02, 0x1a, 0x11,
                                                      0x00, 0x00, 0x00};
static constexpr uint8_t kGuestMacAddress[ETH_ALEN] = {0x02, 0x1a, 0x11,
                                                       0x00, 0x01, 0x00};

static constexpr uint8_t kHostIpv4Address[4] = {192, 168, 0, 1};
static constexpr uint8_t kGuestIpv4Address[4] = {192, 168, 0, 10};

static constexpr uint16_t kProtocolIpv4 = 0x0800;
static constexpr uint8_t kPacketTypeUdp = 17;
static constexpr uint16_t kTestPort = 4242;

void MockNetstack::AddEthernetDevice(
    std::string topological_path,
    fuchsia::netstack::InterfaceConfig interfaceConfig,
    fidl::InterfaceHandle<::fuchsia::hardware::ethernet::Device> device,
    AddEthernetDeviceCallback callback) {
  auto deferred =
      fit::defer([callback = std::move(callback)]() { callback(0); });
  eth_device_ = device.BindSync();

  zx_status_t status;
  std::unique_ptr<fuchsia::hardware::ethernet::Fifos> fifos;
  eth_device_->GetFifos(&status, &fifos);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get fifos: " << status;
    return;
  }
  rx_ = std::move(fifos->rx);
  tx_ = std::move(fifos->tx);

  status = zx::vmo::create(kVmoSize, ZX_VMO_NON_RESIZABLE, &vmo_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create vmo: " << status;
    return;
  }

  zx::vmo vmo_dup;
  status =
      vmo_.duplicate(ZX_RIGHTS_IO | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER, &vmo_dup);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to duplicate vmo: " << status;
    return;
  }

  eth_device_->SetIOBuffer(std::move(vmo_dup), &status);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to set IO buffer: " << status;
    return;
  }

  status = zx::vmar::root_self()->map(
      0, vmo_, 0, kVmoSize,
      ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_REQUIRE_NON_RESIZABLE,
      &io_addr_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map vmo: " << status;
    return;
  }

  eth_fifo_entry_t entry;
  entry.offset = 0;
  entry.length = kMtu;
  entry.flags = 0;
  entry.cookie = 0;
  status = rx_.write(sizeof(eth_fifo_entry_t), &entry, 1,
                     nullptr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to write to rx fifo: " << status;
    return;
  }

  eth_device_->Start(&status);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to start ethernet device: " << status;
    return;
  }
}

static uint16_t checksum(const void* _data, size_t len, uint16_t _sum) {
  uint32_t sum = _sum;
  auto data = static_cast<const uint16_t*>(_data);
  for (;len > 1; len -= 2) {
    sum += *data++;
  }
  if (len) {
    sum += (*data & UINT8_MAX);
  }
  while (sum > UINT16_MAX) {
    sum = (sum & UINT16_MAX) + (sum >> 16);
  }
  return ~sum;
}

static size_t make_ip_header(uint8_t packet_type, size_t length, uint8_t* data) {
  // First construct the ethernet header.
  ethhdr* eth = reinterpret_cast<ethhdr*>(data);
  memcpy(eth->h_dest, kGuestMacAddress, ETH_ALEN);
  memcpy(eth->h_source, kHostMacAddress, ETH_ALEN);
  eth->h_proto = htons(kProtocolIpv4);

  // Now construct the IPv4 header.
  auto ip = reinterpret_cast<iphdr*>(data + sizeof(ethhdr));
  ip->version = 4;
  ip->ihl = sizeof(iphdr) >> 2; // Header length in 32-bit words.
  ip->tos = 0;
  ip->tot_len = htons(sizeof(iphdr) + length);
  ip->id = 0;
  ip->frag_off = 0;
  ip->ttl = UINT8_MAX;
  ip->protocol = packet_type;
  memcpy(&ip->saddr, kHostIpv4Address, sizeof(kHostIpv4Address));
  memcpy(&ip->daddr, kGuestIpv4Address, sizeof(kGuestIpv4Address));
  ip->check = 0;
  ip->check = checksum(ip, sizeof(iphdr), 0);

  return sizeof(ethhdr) + sizeof(iphdr);
}

zx_status_t MockNetstack::SendUdpPacket(void* packet, size_t length) const {
  struct udp_hdr_t {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
  } __PACKED;

  size_t packet_length = sizeof(udp_hdr_t) + length;
  size_t total_length = sizeof(ethhdr) + sizeof(iphdr) + packet_length;
  if (total_length > kMtu) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  uint8_t data[kMtu];
  size_t header_len = make_ip_header(kPacketTypeUdp, packet_length, data);

  uintptr_t off = header_len;
  auto udp = reinterpret_cast<udp_hdr_t*>(data + off);
  udp->src_port = htons(kTestPort);
  udp->dst_port = htons(kTestPort);
  udp->length = htons(sizeof(udp_hdr_t) + length);
  // The checksum is optional for IPv4.
  udp->checksum = 0;

  off += sizeof(udp_hdr_t);
  memcpy(data + off, packet, length);

  return SendPacket(data, total_length);
}

zx_status_t MockNetstack::SendPacket(void* packet, size_t length) const {
  if (length > kMtu) {
    return ZX_ERR_INVALID_ARGS;
  }

  eth_fifo_entry_t entry;
  entry.offset = kMtu;
  entry.length = length;
  entry.flags = 0;
  entry.cookie = 0;
  size_t count;
  memcpy(reinterpret_cast<void*>(io_addr_ + entry.offset), packet, length);
  zx_status_t status = tx_.write(sizeof(eth_fifo_entry_t),
                                 &entry, 1, &count);
  if (status != ZX_OK) {
    return status;
  }
  if (count != 1) {
    return ZX_ERR_INTERNAL;
  }

  zx_signals_t pending = 0;
  status = tx_.wait_one(ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED,
                        zx::deadline_after(kTestTimeout), &pending);
  if (status != ZX_OK) {
    return status;
  } else if (pending & ZX_SOCKET_PEER_CLOSED) {
    return ZX_ERR_PEER_CLOSED;
  }

  status = tx_.read(sizeof(eth_fifo_entry_t), &entry, 1,
                    nullptr);
  if (status != ZX_OK) {
    return status;
  }
  if (entry.flags != ETH_FIFO_TX_OK) {
    return ZX_ERR_IO;
  }

  return ZX_OK;
}

zx_status_t MockNetstack::ReceivePacket(void* packet, size_t length,
                                        size_t* actual) const {
  eth_fifo_entry_t entry;

  zx_signals_t pending = 0;
  zx_status_t status = rx_.wait_one(ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED,
                                    zx::deadline_after(kTestTimeout), &pending);
  if (status != ZX_OK) {
    return status;
  } else if (pending & ZX_SOCKET_PEER_CLOSED) {
    return ZX_ERR_PEER_CLOSED;
  }

  status = rx_.read(sizeof(eth_fifo_entry_t), &entry, 1,
                    nullptr);
  if (status != ZX_OK) {
    return status;
  }
  if (entry.flags != ETH_FIFO_RX_OK) {
    return ZX_ERR_IO;
  }
  if (entry.length > length) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  memcpy(packet, reinterpret_cast<void*>(io_addr_ + entry.offset), length);
  *actual = entry.length;

  memset(reinterpret_cast<void*>(io_addr_), 0, kMtu);
  entry.offset = 0;
  entry.length = kMtu;
  entry.flags = 0;
  entry.cookie = 0;
  status = rx_.write(sizeof(eth_fifo_entry_t), &entry, 1,
                     nullptr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to write to rx fifo: " << status;
    return status;
  }

  return ZX_OK;
}