// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/mdns_fidl_util.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include "lib/fidl/cpp/type_converter.h"
#include "src/lib/fsl/types/type_converters.h"

namespace mdns {

// static
const std::string MdnsFidlUtil::kFuchsiaServiceName = "_fuchsia._tcp.";

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

// static
std::unique_ptr<Mdns::Publication> MdnsFidlUtil::Convert(
    const fuchsia::net::mdns::PublicationPtr& publication_ptr) {
  if (!publication_ptr) {
    return nullptr;
  }

  auto publication =
      Mdns::Publication::Create(inet::IpPort::From_uint16_t(publication_ptr->port),
                                fidl::To<std::vector<std::string>>(publication_ptr->text),
                                publication_ptr->srv_priority, publication_ptr->srv_weight);

  auto ensure_uint32_secs = [](int64_t nanos) -> uint32_t {
    const int64_t secs = zx::nsec(nanos).to_secs();
    FX_CHECK(secs >= 0 && secs < std::numeric_limits<uint32_t>::max())
        << secs << " doesn't fit a uint32";
    return static_cast<uint32_t>(secs);
  };

  publication->ptr_ttl_seconds_ = ensure_uint32_secs(publication_ptr->ptr_ttl);
  publication->srv_ttl_seconds_ = ensure_uint32_secs(publication_ptr->srv_ttl);
  publication->txt_ttl_seconds_ = ensure_uint32_secs(publication_ptr->txt_ttl);

  return publication;
}

// static
std::vector<fuchsia::net::IpAddress> MdnsFidlUtil::Convert(
    const std::vector<inet::SocketAddress>& addresses) {
  std::vector<fuchsia::net::IpAddress> result;
  for (auto& address : addresses) {
    result.push_back(MdnsFidlUtil::CreateIpAddress(address.address()));
  }

  return result;
}

// static
fuchsia::net::mdns::ResourceType MdnsFidlUtil::Convert(DnsType type) {
  switch (type) {
    case DnsType::kPtr:
      return fuchsia::net::mdns::ResourceType::PTR;
    case DnsType::kAny:
      return fuchsia::net::mdns::ResourceType::ANY;
    default:
      FX_DCHECK(false) << "Asked to convert unexpected DnsType " << static_cast<uint32_t>(type);
      return fuchsia::net::mdns::ResourceType::ANY;
  }
}

void MdnsFidlUtil::FillServiceInstance(fuchsia::net::mdns::ServiceInstance* service_instance,
                                       const std::string& service, const std::string& instance,
                                       const inet::SocketAddress& v4_address,
                                       const inet::SocketAddress& v6_address,
                                       const std::vector<std::string>& text, uint16_t srv_priority,
                                       uint16_t srv_weight, const std::string& target) {
  service_instance->set_service(service);
  service_instance->set_instance(instance);
  service_instance->set_text(text);
  service_instance->set_srv_priority(srv_priority);
  service_instance->set_srv_weight(srv_weight);
  service_instance->set_target(target);
  if (v4_address) {
    service_instance->set_ipv4_endpoint(MdnsFidlUtil::CreateSocketAddressV4(v4_address));
  }
  if (v6_address) {
    service_instance->set_ipv6_endpoint(MdnsFidlUtil::CreateSocketAddressV6(v6_address));
  }
}

}  // namespace mdns
