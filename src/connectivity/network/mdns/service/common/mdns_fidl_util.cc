// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/common/mdns_fidl_util.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include "fuchsia/net/cpp/fidl.h"
#include "src/connectivity/network/mdns/service/common/type_converters.h"

namespace mdns {

// static
fuchsia::net::Ipv4Address MdnsFidlUtil::CreateIpv4Address(const inet::IpAddress& ip_address) {
  FX_DCHECK(ip_address);
  FX_DCHECK(ip_address.is_v4());

  fuchsia::net::Ipv4Address addr;
  FX_DCHECK(addr.addr.size() == ip_address.byte_count());
  std::memcpy(addr.addr.data(), ip_address.as_bytes(), addr.addr.size());

  return addr;
}

// static
fuchsia::net::Ipv6Address MdnsFidlUtil::CreateIpv6Address(const inet::IpAddress& ip_address) {
  FX_DCHECK(ip_address);
  FX_DCHECK(ip_address.is_v6());

  fuchsia::net::Ipv6Address addr;
  FX_DCHECK(addr.addr.size() == ip_address.byte_count());
  std::memcpy(addr.addr.data(), ip_address.as_bytes(), addr.addr.size());

  return addr;
}

// static
fuchsia::net::IpAddress MdnsFidlUtil::CreateIpAddress(const inet::IpAddress& ip_address) {
  FX_DCHECK(ip_address);
  fuchsia::net::IpAddress result;

  if (ip_address.is_v4()) {
    result.set_ipv4(CreateIpv4Address(ip_address));
  } else {
    result.set_ipv6(CreateIpv6Address(ip_address));
  }

  return result;
}

// static
fuchsia::net::Ipv4SocketAddress MdnsFidlUtil::CreateSocketAddressV4(
    const inet::SocketAddress& socket_address) {
  FX_DCHECK(socket_address);
  FX_DCHECK(socket_address.is_v4());

  return fuchsia::net::Ipv4SocketAddress{CreateIpv4Address(socket_address.address()),
                                         socket_address.port().as_uint16_t()};
}

// static
fuchsia::net::Ipv6SocketAddress MdnsFidlUtil::CreateSocketAddressV6(
    const inet::SocketAddress& socket_address) {
  FX_DCHECK(socket_address);
  FX_DCHECK(socket_address.is_v6());

  return fuchsia::net::Ipv6SocketAddress{CreateIpv6Address(socket_address.address()),
                                         socket_address.port().as_uint16_t(),
                                         socket_address.scope_id()};
}

// static
inet::IpAddress MdnsFidlUtil::IpAddressFrom(const fuchsia::net::IpAddress& addr) {
  switch (addr.Which()) {
    case fuchsia::net::IpAddress::Tag::kIpv4:
      FX_DCHECK(addr.ipv4().addr.size() == sizeof(in_addr));
      return inet::IpAddress(*reinterpret_cast<const in_addr*>(addr.ipv4().addr.data()));
    case fuchsia::net::IpAddress::Tag::kIpv6:
      FX_DCHECK(addr.ipv6().addr.size() == sizeof(in6_addr));
      return inet::IpAddress(*reinterpret_cast<const in6_addr*>(addr.ipv6().addr.data()));
    case fuchsia::net::IpAddress::Tag::Invalid:
      return inet::IpAddress();
  }
}

void MdnsFidlUtil::FillServiceInstance(fuchsia::net::mdns::ServiceInstance* service_instance,
                                       const std::string& service, const std::string& instance,
                                       const std::vector<inet::SocketAddress>& addresses,
                                       const std::vector<std::vector<uint8_t>>& text,
                                       uint16_t srv_priority, uint16_t srv_weight,
                                       const std::string& target) {
  service_instance->set_service(service);
  service_instance->set_instance(instance);
  service_instance->set_text(fidl::To<std::vector<std::string>>(text));
  service_instance->set_srv_priority(srv_priority);
  service_instance->set_srv_weight(srv_weight);
  service_instance->set_target(target);

  for (const auto& address : addresses) {
    if (address.is_v4()) {
      if (!service_instance->has_ipv4_endpoint()) {
        service_instance->set_ipv4_endpoint(MdnsFidlUtil::CreateSocketAddressV4(address));
      }
    } else {
      if (!service_instance->has_ipv6_endpoint()) {
        service_instance->set_ipv6_endpoint(MdnsFidlUtil::CreateSocketAddressV6(address));
      }
    }
  }

  service_instance->set_addresses(fidl::To<std::vector<fuchsia::net::SocketAddress>>(addresses));
  service_instance->set_text_strings(text);
}

}  // namespace mdns
