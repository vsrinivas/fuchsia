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
static constexpr size_t kIp6HeaderLength = sizeof(ethhdr) + sizeof(ip6_hdr);

static constexpr uint8_t kHostMacAddress[ETH_ALEN] = {0x02, 0x1a, 0x11,
                                                      0x00, 0x00, 0x00};
static constexpr uint8_t kGuestMacAddress[ETH_ALEN] = {0x02, 0x1a, 0x11,
                                                       0x00, 0x01, 0x00};

static constexpr uint8_t kHostIpv6Address[16] = {0xfe, 0x80, 0, 0, 0, 0, 0, 0,
                                                 0,    0,    0, 0, 0, 0, 0, 1};
static constexpr uint8_t kBroadcastIpv6Address[16] = {
    0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};

static constexpr uint16_t kProtocolIpv6 = 0x86dd;
static constexpr uint8_t kIp6Version = 0x60;
static constexpr uint8_t kPacketTypeUdp = 17;
static constexpr uint8_t kPacketTypeIcmp = 58;

static constexpr uint8_t kNdpFlagOverride = 0x20;
static constexpr uint8_t kNdpOptionTargetLinkLayerAddress = 2;

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

static void make_ip_header(uint8_t packet_type, size_t length, uint8_t* data) {
  // First construct the ethernet header.
  ethhdr* eth = reinterpret_cast<ethhdr*>(data);
  memcpy(eth->h_dest, kGuestMacAddress, ETH_ALEN);
  memcpy(eth->h_source, kHostMacAddress, ETH_ALEN);
  eth->h_proto = htons(kProtocolIpv6);

  // Now construct the IPv6 header.
  auto ip = reinterpret_cast<ip6_hdr*>(data + sizeof(ethhdr));
  ip->ip6_vfc = kIp6Version;
  ip->ip6_plen = htons(length);
  ip->ip6_nxt = packet_type;
  ip->ip6_hlim = 1;  // Hop limit.
  memcpy(&ip->ip6_src, kHostIpv6Address, sizeof(kHostIpv6Address));
  memcpy(&ip->ip6_dst, kBroadcastIpv6Address, sizeof(kBroadcastIpv6Address));
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
  return sum;
}

static unsigned ip6_checksum(size_t length, uint8_t* data) {
  uint16_t sum;
  auto ip = reinterpret_cast<ip6_hdr*>(data + sizeof(ethhdr));

  // Length and protocol fields.
  sum = checksum(&ip->ip6_plen, 2, htons(ip->ip6_nxt));
  // Source, destination, and payload.
  sum = checksum(&ip->ip6_src, 32 + length, sum);

  // 0 is illegal, so 0xffff remains 0xffff
  if (sum != 0xffff) {
    return ~sum;
  } else {
    return sum;
  }
}

zx_status_t MockNetstack::SendAdvertisement() const {
  struct icmp6_hdr_t {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
  } __PACKED;

  struct ndp_n_hdr_t {
    uint32_t flags;
    uint8_t target[16];
  } __PACKED;

  struct ndp_opt_target_ll_addr_t {
    uint8_t type;
    uint8_t length;
    uint8_t address[ETH_ALEN];
  } __PACKED;

  size_t length = sizeof(icmp6_hdr_t) + sizeof(ndp_n_hdr_t) +
                  sizeof(ndp_opt_target_ll_addr_t);
  if (kIp6HeaderLength + length > kMtu) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  uint8_t data[kMtu];
  make_ip_header(kPacketTypeIcmp, length, data);
  uintptr_t off = kIp6HeaderLength;

  // Construct the ICMP6 header. Indicate that the packet is using the Neighbor Discovery Protocol
  // (NDP) and that this is an advertisement.
  auto icmp = reinterpret_cast<icmp6_hdr_t*>(data + off);
  icmp->type = ND_NEIGHBOR_ADVERT;
  icmp->code = 0;
  off += sizeof(icmp6_hdr_t);

  // Construct the NDP header. This contains the target's (i.e. host's) IPv6 address. We set the
  // flags to override any previously received information, although this probably isn't strictly
  // necessary.
  auto ndp = reinterpret_cast<ndp_n_hdr_t*>(data + off);
  ndp->flags = kNdpFlagOverride;
  memcpy(ndp->target, kHostIpv6Address, sizeof(kHostIpv6Address));
  off += sizeof(ndp_n_hdr_t);

  // Construct a single NDP option containing the target's (i.e. host's) link-layer address, i.e.
  // its MAC address.
  auto option = reinterpret_cast<ndp_opt_target_ll_addr_t*>(data + off);
  option->type = kNdpOptionTargetLinkLayerAddress;
  option->length = 1;
  memcpy(option->address, kHostMacAddress, ETH_ALEN);
  off += sizeof(ndp_opt_target_ll_addr_t);

  icmp->checksum = ip6_checksum(length, data);

  return SendPacket(data, off);
}

zx_status_t MockNetstack::SendUdpPacket(void* packet, size_t length) const {
  struct udp_hdr_t {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
  } __PACKED;

  size_t packet_length = sizeof(udp_hdr_t) + length;
  if (kIp6HeaderLength + packet_length > kMtu) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  uint8_t data[kMtu];
  make_ip_header(kPacketTypeUdp, packet_length, data);
  uintptr_t off = kIp6HeaderLength;

  auto udp = reinterpret_cast<udp_hdr_t*>(data + off);
  udp->src_port = htons(4242);
  udp->dst_port = htons(4242);
  udp->length = htons(sizeof(udp_hdr_t) + length);
  udp->checksum = 0;

  off += sizeof(udp_hdr_t);
  memcpy(data + off, packet, length);

  udp->checksum = ip6_checksum(packet_length, data);

  return SendPacket(data, kIp6HeaderLength + packet_length);
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