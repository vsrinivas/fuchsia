// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.posix.socket.packet/cpp/wire.h>
#include <fidl/fuchsia.posix.socket/cpp/wire.h>
#include <lib/zxio/cpp/socket_address.h>
#include <netinet/icmp6.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <zircon/types.h>

#include <functional>
#include <variant>

#include <netpacket/packet.h>

#include "sdk/lib/zxio/hash.h"

namespace fpacketsocket = fuchsia_posix_socket_packet;
namespace fnet = fuchsia_net;

std::optional<SocketAddress> SocketAddress::FromFidl(
    const fuchsia_net::wire::SocketAddress& from_addr) {
  if (from_addr.has_invalid_tag()) {
    return std::nullopt;
  }
  SocketAddress addr;
  switch (from_addr.Which()) {
    case fuchsia_net::wire::SocketAddress::Tag::kIpv4: {
      addr.storage_ = from_addr.ipv4();
    } break;
    case fuchsia_net::wire::SocketAddress::Tag::kIpv6: {
      addr.storage_ = from_addr.ipv6();
    } break;
  }
  return addr;
}

zx_status_t SocketAddress::LoadSockAddr(const struct sockaddr* addr, size_t addr_len) {
  // Address length larger than sockaddr_storage causes an error for API compatibility only.
  if (addr == nullptr || addr_len > sizeof(struct sockaddr_storage)) {
    return ZX_ERR_INVALID_ARGS;
  }
  switch (addr->sa_family) {
    case AF_INET: {
      if (addr_len < sizeof(struct sockaddr_in)) {
        return ZX_ERR_INVALID_ARGS;
      }
      const auto& s = *reinterpret_cast<const struct sockaddr_in*>(addr);
      fuchsia_net::wire::Ipv4SocketAddress address = {
          .port = ntohs(s.sin_port),
      };
      static_assert(sizeof(address.address.addr) == sizeof(s.sin_addr.s_addr),
                    "size of IPv4 addresses should be the same");
      memcpy(address.address.addr.data(), &s.sin_addr.s_addr, sizeof(s.sin_addr.s_addr));
      storage_ = address;
      return ZX_OK;
    }
    case AF_INET6: {
      if (addr_len < sizeof(struct sockaddr_in6)) {
        return ZX_ERR_INVALID_ARGS;
      }
      const auto& s = *reinterpret_cast<const struct sockaddr_in6*>(addr);
      fuchsia_net::wire::Ipv6SocketAddress address = {
          .port = ntohs(s.sin6_port),
          .zone_index = s.sin6_scope_id,
      };
      static_assert(
          decltype(address.address.addr)::size() == std::size(decltype(s.sin6_addr.s6_addr){}),
          "size of IPv6 addresses should be the same");
      std::copy(std::begin(s.sin6_addr.s6_addr), std::end(s.sin6_addr.s6_addr),
                address.address.addr.begin());
      storage_ = address;
      return ZX_OK;
    }
    default:
      return ZX_ERR_INVALID_ARGS;
  }
}

bool SocketAddress::operator==(const SocketAddress& o) const {
  if (!storage_.has_value()) {
    return !o.storage_.has_value();
  }
  if (!o.storage_.has_value()) {
    return false;
  }
  return std::visit(
      overloaded{
          [&o](const fuchsia_net::wire::Ipv4SocketAddress& ipv4) {
            return std::visit(
                overloaded{
                    [&ipv4](const fuchsia_net::wire::Ipv4SocketAddress& other_ipv4) {
                      return ipv4.port == other_ipv4.port &&
                             std::equal(ipv4.address.addr.begin(), ipv4.address.addr.end(),
                                        other_ipv4.address.addr.begin(),
                                        other_ipv4.address.addr.end());
                    },
                    [](const fuchsia_net::wire::Ipv6SocketAddress& ipv6) { return false; },
                },
                o.storage_.value());
          },
          [&o](const fuchsia_net::wire::Ipv6SocketAddress& ipv6) {
            return std::visit(
                overloaded{
                    [](const fuchsia_net::wire::Ipv4SocketAddress& ipv4) { return false; },
                    [&ipv6](const fuchsia_net::wire::Ipv6SocketAddress& other_ipv6) {
                      return ipv6.port == other_ipv6.port &&
                             ipv6.zone_index == other_ipv6.zone_index &&
                             std::equal(ipv6.address.addr.begin(), ipv6.address.addr.end(),
                                        other_ipv6.address.addr.begin(),
                                        other_ipv6.address.addr.end());
                    },
                },
                o.storage_.value());
          },
      },
      storage_.value());
}

bool SocketAddress::operator!=(const SocketAddress& o) const { return !operator==(o); }

size_t SocketAddress::hash() const {
  if (!storage_.has_value()) {
    return 0;
  }
  return std::visit(overloaded{
                        [](const fuchsia_net::wire::Ipv4SocketAddress& ipv4) {
                          size_t h = std::hash<fuchsia_net::wire::SocketAddress::Tag>()(
                              fuchsia_net::wire::SocketAddress::Tag::kIpv4);
                          for (const auto& addr_bits : ipv4.address.addr) {
                            hash_combine(h, addr_bits);
                          }
                          hash_combine(h, ipv4.port);
                          return h;
                        },
                        [](const fuchsia_net::wire::Ipv6SocketAddress& ipv6) {
                          size_t h = std::hash<fuchsia_net::wire::SocketAddress::Tag>()(
                              fuchsia_net::wire::SocketAddress::Tag::kIpv6);
                          for (const auto& addr_bits : ipv6.address.addr) {
                            hash_combine(h, addr_bits);
                          }
                          hash_combine(h, ipv6.port);
                          hash_combine(h, ipv6.zone_index);
                          return h;
                        },
                    },
                    storage_.value());
}

socklen_t zxio_fidl_to_sockaddr(const fuchsia_net::wire::SocketAddress& fidl, void* addr,
                                socklen_t addr_len) {
  switch (fidl.Which()) {
    case fuchsia_net::wire::SocketAddress::Tag::kIpv4: {
      const auto& ipv4 = fidl.ipv4();
      struct sockaddr_in tmp = {
          .sin_family = AF_INET,
          .sin_port = htons(ipv4.port),
      };
      static_assert(sizeof(tmp.sin_addr.s_addr) == sizeof(ipv4.address.addr),
                    "size of IPv4 addresses should be the same");
      memcpy(&tmp.sin_addr.s_addr, ipv4.address.addr.data(), sizeof(ipv4.address.addr));
      // Copy truncated address.
      memcpy(addr, &tmp, std::min(sizeof(tmp), static_cast<size_t>(addr_len)));
      return sizeof(tmp);
    }
    case fuchsia_net::wire::SocketAddress::Tag::kIpv6: {
      const auto& ipv6 = fidl.ipv6();
      struct sockaddr_in6 tmp = {
          .sin6_family = AF_INET6,
          .sin6_port = htons(ipv6.port),
          .sin6_scope_id = static_cast<uint32_t>(ipv6.zone_index),
      };
      static_assert(std::size(tmp.sin6_addr.s6_addr) == decltype(ipv6.address.addr)::size(),
                    "size of IPv6 addresses should be the same");
      std::copy(ipv6.address.addr.begin(), ipv6.address.addr.end(),
                std::begin(tmp.sin6_addr.s6_addr));
      // Copy truncated address.
      memcpy(addr, &tmp, std::min(sizeof(tmp), static_cast<size_t>(addr_len)));
      return sizeof(tmp);
    }
  }
}

uint16_t zxio_fidl_hwtype_to_arphrd(const fpacketsocket::wire::HardwareType type) {
  switch (type) {
    case fpacketsocket::wire::HardwareType::kNetworkOnly:
      return ARPHRD_NONE;
    case fpacketsocket::wire::HardwareType::kEthernet:
      return ARPHRD_ETHER;
    case fpacketsocket::wire::HardwareType::kLoopback:
      return ARPHRD_LOOPBACK;
  }
}

void zxio_populate_from_fidl_hwaddr(const fpacketsocket::wire::HardwareAddress& addr,
                                    sockaddr_ll& s) {
  switch (addr.Which()) {
    case fpacketsocket::wire::HardwareAddress::Tag::kUnknown:
      // The server is newer than us and sending a variant we don't understand.
      __FALLTHROUGH;
    case fpacketsocket::wire::HardwareAddress::Tag::kNone:
      s.sll_halen = 0;
      break;
    case fpacketsocket::wire::HardwareAddress::Tag::kEui48: {
      const fnet::wire::MacAddress& eui48 = addr.eui48();
      static_assert(std::size(decltype(s.sll_addr){}) == decltype(eui48.octets)::size() + 2);
      std::copy(eui48.octets.begin(), eui48.octets.end(), std::begin(s.sll_addr));
      s.sll_halen = decltype(eui48.octets)::size();
    } break;
  }
}
