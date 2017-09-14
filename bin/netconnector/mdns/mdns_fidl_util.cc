// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/netconnector/mdns/mdns_fidl_util.h"

#include "lib/fxl/logging.h"

namespace netconnector {
namespace mdns {

// static
const std::string MdnsFidlUtil::kFuchsiaServiceName = "_fuchsia._tcp.";

// static
MdnsServiceInstancePtr MdnsFidlUtil::CreateServiceInstance(
    const std::string& service_name,
    const std::string& instance_name,
    const SocketAddress& v4_address,
    const SocketAddress& v6_address,
    const std::vector<std::string>& text) {
  MdnsServiceInstancePtr service_instance = MdnsServiceInstance::New();

  service_instance->service_name = service_name;
  service_instance->instance_name = instance_name;
  service_instance->text = fidl::Array<fidl::String>::From(text);

  if (v4_address.is_valid()) {
    service_instance->v4_address = CreateSocketAddressIPv4(v4_address);
  }

  if (v6_address.is_valid()) {
    service_instance->v6_address = CreateSocketAddressIPv6(v6_address);
  }

  return service_instance;
}

// static
bool MdnsFidlUtil::UpdateServiceInstance(
    const MdnsServiceInstancePtr& service_instance,
    const SocketAddress& v4_address,
    const SocketAddress& v6_address,
    const std::vector<std::string>& text) {
  bool changed = false;

  if (v4_address.is_valid()) {
    if (!service_instance->v4_address) {
      service_instance->v4_address = CreateSocketAddressIPv4(v4_address);
    } else if (UpdateSocketAddressIPv4(service_instance->v4_address,
                                       v4_address)) {
      changed = true;
    }
  } else if (service_instance->v4_address) {
    service_instance->v4_address.reset();
    changed = true;
  }

  if (v6_address.is_valid()) {
    if (!service_instance->v6_address) {
      service_instance->v6_address = CreateSocketAddressIPv6(v6_address);
    } else if (UpdateSocketAddressIPv6(service_instance->v6_address,
                                       v6_address)) {
      changed = true;
    }
  } else if (service_instance->v6_address) {
    service_instance->v6_address.reset();
    changed = true;
  }

  if (service_instance->text.size() != text.size()) {
    service_instance->text.resize(text.size());
    changed = true;
  }

  for (size_t i = 0; i < text.size(); ++i) {
    if (service_instance->text[i] != text[i]) {
      service_instance->text[i] = text[i];
      changed = true;
    }
  }

  return changed;
}

// static
netstack::SocketAddressPtr MdnsFidlUtil::CreateSocketAddressIPv4(
    const IpAddress& ip_address) {
  if (!ip_address) {
    return nullptr;
  }

  FXL_DCHECK(ip_address.is_v4());

  netstack::SocketAddressPtr result = netstack::SocketAddress::New();
  result->addr = netstack::NetAddress::New();
  result->addr->ipv4 = fidl::Array<uint8_t>::New(4);
  result->port = 0;

  FXL_DCHECK(result->addr->ipv4.size() == ip_address.byte_count());
  std::memcpy(result->addr->ipv4.data(), ip_address.as_bytes(),
              result->addr->ipv4.size());

  return result;
}

// static
netstack::SocketAddressPtr MdnsFidlUtil::CreateSocketAddressIPv6(
    const IpAddress& ip_address) {
  if (!ip_address) {
    return nullptr;
  }

  FXL_DCHECK(ip_address.is_v6());

  netstack::SocketAddressPtr result = netstack::SocketAddress::New();
  result->addr = netstack::NetAddress::New();
  result->addr->ipv6 = fidl::Array<uint8_t>::New(16);
  result->port = 0;

  FXL_DCHECK(result->addr->ipv6.size() == ip_address.byte_count());
  std::memcpy(result->addr->ipv6.data(), ip_address.as_bytes(),
              result->addr->ipv6.size());

  return result;
}

// static
netstack::SocketAddressPtr MdnsFidlUtil::CreateSocketAddressIPv4(
    const SocketAddress& socket_address) {
  if (!socket_address) {
    return nullptr;
  }

  FXL_DCHECK(socket_address.is_v4());

  netstack::SocketAddressPtr result =
      CreateSocketAddressIPv4(socket_address.address());

  result->port = socket_address.port().as_uint16_t();

  return result;
}

// static
netstack::SocketAddressPtr MdnsFidlUtil::CreateSocketAddressIPv6(
    const SocketAddress& socket_address) {
  if (!socket_address) {
    return nullptr;
  }

  FXL_DCHECK(socket_address.is_v6());

  netstack::SocketAddressPtr result =
      CreateSocketAddressIPv6(socket_address.address());

  result->port = socket_address.port().as_uint16_t();

  return result;
}

// static
bool MdnsFidlUtil::UpdateSocketAddressIPv4(
    const netstack::SocketAddressPtr& net_address,
    const SocketAddress& socket_address) {
  FXL_DCHECK(net_address);
  FXL_DCHECK(socket_address.is_v4());

  bool changed = false;

  if (net_address->port != socket_address.port().as_uint16_t()) {
    net_address->port = socket_address.port().as_uint16_t();
    changed = true;
  }

  FXL_DCHECK(net_address->addr->ipv4.size() ==
             socket_address.address().byte_count());
  if (std::memcmp(net_address->addr->ipv4.data(),
                  socket_address.address().as_bytes(),
                  net_address->addr->ipv4.size()) != 0) {
    std::memcpy(net_address->addr->ipv4.data(),
                socket_address.address().as_bytes(),
                net_address->addr->ipv4.size());
    changed = true;
  }

  return changed;
}

// static
bool MdnsFidlUtil::UpdateSocketAddressIPv6(
    const netstack::SocketAddressPtr& net_address,
    const SocketAddress& socket_address) {
  FXL_DCHECK(net_address);
  FXL_DCHECK(socket_address.is_v4());

  bool changed = false;

  if (net_address->port != socket_address.port().as_uint16_t()) {
    net_address->port = socket_address.port().as_uint16_t();
    changed = true;
  }

  FXL_DCHECK(net_address->addr->ipv6.size() ==
             socket_address.address().byte_count());
  if (std::memcmp(net_address->addr->ipv6.data(),
                  socket_address.address().as_bytes(),
                  net_address->addr->ipv6.size()) != 0) {
    std::memcpy(net_address->addr->ipv6.data(),
                socket_address.address().as_bytes(),
                net_address->addr->ipv6.size());
    changed = true;
  }

  return changed;
}

}  // namespace mdns
}  // namespace netconnector
