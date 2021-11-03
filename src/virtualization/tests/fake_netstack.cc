// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_netstack.h"

#include <fuchsia/net/interfaces/cpp/fidl_test_base.h>
#include <fuchsia/netstack/cpp/fidl_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/defer.h>
#include <lib/fpromise/bridge.h>
#include <lib/sys/cpp/testing/enclosing_environment.h>
#include <lib/syslog/cpp/macros.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <zircon/device/ethernet.h>
#include <zircon/status.h>

#include <queue>

#include "fake_netstack_v1.h"

static constexpr uint32_t kMtu = 1500;

static constexpr uint8_t kHostMacAddress[ETH_ALEN] = {0x02, 0x1a, 0x11, 0x00, 0x00, 0x00};

static constexpr uint8_t kHostIpv4Address[4] = {192, 168, 0, 1};
static constexpr uint8_t kGuestIpv4Address[4] = {192, 168, 0, 10};

static constexpr uint16_t kProtocolIpv4 = 0x0800;
static constexpr uint8_t kPacketTypeUdp = 17;
static constexpr uint16_t kTestPort = 4242;

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

void FakeNetstack::Install(sys::testing::EnvironmentServices& services) {
  zx_status_t status =
      services.AddService(state_v1_.GetHandler(), fuchsia::net::interfaces::State::Name_);
  FX_CHECK(status == ZX_OK) << "Failure installing FakeState into environment: "
                            << zx_status_get_string(status);

  status = services.AddService(netstack_v1_.GetHandler(), fuchsia::netstack::Netstack::Name_);
  FX_CHECK(status == ZX_OK) << "Failure installing FakeNetstack into environment: "
                            << zx_status_get_string(status);
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

fpromise::promise<void, zx_status_t> FakeNetstack::SendPacket(
    const fuchsia::hardware::ethernet::MacAddress& mac_addr, std::vector<uint8_t> packet) {
  if (packet.size() > kMtu) {
    return fpromise::make_error_promise(ZX_ERR_INVALID_ARGS);
  }

  return netstack_v1_.GetDevice(mac_addr).then(
      [packet =
           std::move(packet)](const fpromise::result<fake_netstack::v1::Device*>& result) mutable
      -> fpromise::promise<void, zx_status_t> {
        if (!result.is_ok()) {
          return fpromise::make_error_promise(ZX_ERR_INTERNAL);
        }
        fake_netstack::v1::Device* device = result.value();
        return device->WritePacket(std::move(packet));
      });
}

fpromise::promise<std::vector<uint8_t>, zx_status_t> FakeNetstack::ReceivePacket(
    const fuchsia::hardware::ethernet::MacAddress& mac_addr) {
  return netstack_v1_.GetDevice(mac_addr).then(
      [](const fpromise::result<fake_netstack::v1::Device*>& result)
          -> fpromise::promise<std::vector<uint8_t>, zx_status_t> {
        if (!result.is_ok()) {
          return fpromise::make_result_promise<std::vector<uint8_t>, zx_status_t>(
              fpromise::error(ZX_ERR_INTERNAL));
        }
        fake_netstack::v1::Device* device = result.value();
        return device->ReadPacket();
      });
}
