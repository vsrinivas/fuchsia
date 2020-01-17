// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/mdns_fidl_util.h"

#include <lib/zx/time.h>

#include "lib/fidl/cpp/type_converter.h"
#include "src/lib/fsl/types/type_converters.h"
#include "src/lib/fxl/logging.h"

namespace mdns {

// static
const std::string MdnsFidlUtil::kFuchsiaServiceName = "_fuchsia._tcp.";

// static
fuchsia::net::mdns::ServiceInstancePtr MdnsFidlUtil::CreateServiceInstance(
    const std::string& service, const std::string& instance, const inet::SocketAddress& v4_address,
    const inet::SocketAddress& v6_address, const std::vector<std::string>& text) {
  fuchsia::net::mdns::ServiceInstancePtr service_instance =
      fuchsia::net::mdns::ServiceInstance::New();

  service_instance->service = service;
  service_instance->instance = instance;
  service_instance->text = text;

  if (v4_address.is_valid()) {
    service_instance->endpoints.push_back(CreateEndpointV4(v4_address));
  }

  if (v6_address.is_valid()) {
    service_instance->endpoints.push_back(CreateEndpointV6(v6_address));
  }

  return service_instance;
}

// static
void MdnsFidlUtil::UpdateServiceInstance(
    const fuchsia::net::mdns::ServiceInstancePtr& service_instance,
    const inet::SocketAddress& v4_address, const inet::SocketAddress& v6_address,
    const std::vector<std::string>& text) {
  service_instance->text = text;

  service_instance->endpoints.clear();

  if (v4_address.is_valid()) {
    service_instance->endpoints.push_back(CreateEndpointV4(v4_address));
  }

  if (v6_address.is_valid()) {
    service_instance->endpoints.push_back(CreateEndpointV6(v6_address));
  }
}

// static
fuchsia::net::Ipv4Address MdnsFidlUtil::CreateIpv4Address(const inet::IpAddress& ip_address) {
  FXL_DCHECK(ip_address);
  FXL_DCHECK(ip_address.is_v4());

  fuchsia::net::Ipv4Address addr;
  FXL_DCHECK(addr.addr.size() == ip_address.byte_count());
  std::memcpy(addr.addr.data(), ip_address.as_bytes(), addr.addr.size());

  return addr;
}

// static
fuchsia::net::Ipv6Address MdnsFidlUtil::CreateIpv6Address(const inet::IpAddress& ip_address) {
  FXL_DCHECK(ip_address);
  FXL_DCHECK(ip_address.is_v6());

  fuchsia::net::Ipv6Address addr;
  FXL_DCHECK(addr.addr.size() == ip_address.byte_count());
  std::memcpy(addr.addr.data(), ip_address.as_bytes(), addr.addr.size());

  return addr;
}

// static
fuchsia::net::Endpoint MdnsFidlUtil::CreateEndpointV4(const inet::SocketAddress& socket_address) {
  FXL_DCHECK(socket_address);
  FXL_DCHECK(socket_address.is_v4());

  fuchsia::net::Endpoint endpoint;
  endpoint.addr.set_ipv4(CreateIpv4Address(socket_address.address()));
  endpoint.port = socket_address.port().as_uint16_t();

  return endpoint;
}

// static
fuchsia::net::Endpoint MdnsFidlUtil::CreateEndpointV6(const inet::SocketAddress& socket_address) {
  FXL_DCHECK(socket_address);
  FXL_DCHECK(socket_address.is_v6());

  fuchsia::net::Endpoint endpoint;
  endpoint.addr.set_ipv6(CreateIpv6Address(socket_address.address()));
  endpoint.port = socket_address.port().as_uint16_t();

  return endpoint;
}

// static
inet::IpAddress MdnsFidlUtil::IpAddressFrom(const fuchsia::net::IpAddress* addr) {
  FXL_DCHECK(addr != nullptr);
  switch (addr->Which()) {
    case fuchsia::net::IpAddress::Tag::kIpv4:
      if (!addr->is_ipv4()) {
        return inet::IpAddress();
      }

      FXL_DCHECK(addr->ipv4().addr.size() == sizeof(in_addr));
      return inet::IpAddress(*reinterpret_cast<const in_addr*>(addr->ipv4().addr.data()));
    case fuchsia::net::IpAddress::Tag::kIpv6:
      if (!addr->is_ipv6()) {
        return inet::IpAddress();
      }

      FXL_DCHECK(addr->ipv6().addr.size() == sizeof(in6_addr));
      return inet::IpAddress(*reinterpret_cast<const in6_addr*>(addr->ipv6().addr.data()));
    default:
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

  publication->ptr_ttl_seconds_ = zx::nsec(publication_ptr->ptr_ttl).to_secs();
  publication->srv_ttl_seconds_ = zx::nsec(publication_ptr->srv_ttl).to_secs();
  publication->txt_ttl_seconds_ = zx::nsec(publication_ptr->txt_ttl).to_secs();

  return publication;
}

}  // namespace mdns
