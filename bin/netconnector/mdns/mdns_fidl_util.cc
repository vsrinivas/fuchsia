// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/mdns/mdns_fidl_util.h"

#include "lib/ftl/logging.h"

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
    service_instance->v4_address = CreateNetAddressIPv4(v4_address);
  }

  if (v6_address.is_valid()) {
    service_instance->v6_address = CreateNetAddressIPv6(v6_address);
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
      service_instance->v4_address = CreateNetAddressIPv4(v4_address);
    } else if (UpdateNetAddressIPv4(service_instance->v4_address, v4_address)) {
      changed = true;
    }
  } else if (service_instance->v4_address) {
    service_instance->v4_address.reset();
    changed = true;
  }

  if (v6_address.is_valid()) {
    if (!service_instance->v6_address) {
      service_instance->v6_address = CreateNetAddressIPv6(v6_address);
    } else if (UpdateNetAddressIPv6(service_instance->v6_address, v6_address)) {
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
network::NetAddressIPv4Ptr MdnsFidlUtil::CreateNetAddressIPv4(
    const IpAddress& ip_address) {
  if (!ip_address) {
    return nullptr;
  }

  FTL_DCHECK(ip_address.is_v4());

  network::NetAddressIPv4Ptr result = network::NetAddressIPv4::New();
  result->addr = fidl::Array<uint8_t>::New(4);
  result->port = 0;

  FTL_DCHECK(result->addr.size() == ip_address.byte_count());
  std::memcpy(result->addr.data(), ip_address.as_bytes(), result->addr.size());

  return result;
}

// static
network::NetAddressIPv6Ptr MdnsFidlUtil::CreateNetAddressIPv6(
    const IpAddress& ip_address) {
  if (!ip_address) {
    return nullptr;
  }

  FTL_DCHECK(ip_address.is_v6());

  network::NetAddressIPv6Ptr result = network::NetAddressIPv6::New();
  result->addr = fidl::Array<uint8_t>::New(16);
  result->port = 0;

  FTL_DCHECK(result->addr.size() == ip_address.byte_count());
  std::memcpy(result->addr.data(), ip_address.as_bytes(), result->addr.size());

  return result;
}

// static
network::NetAddressIPv4Ptr MdnsFidlUtil::CreateNetAddressIPv4(
    const SocketAddress& socket_address) {
  if (!socket_address) {
    return nullptr;
  }

  FTL_DCHECK(socket_address.is_v4());

  network::NetAddressIPv4Ptr result =
      CreateNetAddressIPv4(socket_address.address());

  result->port = socket_address.port().as_uint16_t();

  return result;
}

// static
network::NetAddressIPv6Ptr MdnsFidlUtil::CreateNetAddressIPv6(
    const SocketAddress& socket_address) {
  if (!socket_address) {
    return nullptr;
  }

  FTL_DCHECK(socket_address.is_v6());

  network::NetAddressIPv6Ptr result =
      CreateNetAddressIPv6(socket_address.address());

  result->port = socket_address.port().as_uint16_t();

  return result;
}

// static
bool MdnsFidlUtil::UpdateNetAddressIPv4(
    const network::NetAddressIPv4Ptr& net_address,
    const SocketAddress& socket_address) {
  FTL_DCHECK(net_address);
  FTL_DCHECK(socket_address.is_v4());

  bool changed = false;

  if (net_address->port != socket_address.port().as_uint16_t()) {
    net_address->port = socket_address.port().as_uint16_t();
    changed = true;
  }

  FTL_DCHECK(net_address->addr.size() == socket_address.address().byte_count());
  if (std::memcmp(net_address->addr.data(), socket_address.address().as_bytes(),
                  net_address->addr.size()) != 0) {
    std::memcpy(net_address->addr.data(), socket_address.address().as_bytes(),
                net_address->addr.size());
    changed = true;
  }

  return changed;
}

// static
bool MdnsFidlUtil::UpdateNetAddressIPv6(
    const network::NetAddressIPv6Ptr& net_address,
    const SocketAddress& socket_address) {
  FTL_DCHECK(net_address);
  FTL_DCHECK(socket_address.is_v4());

  bool changed = false;

  if (net_address->port != socket_address.port().as_uint16_t()) {
    net_address->port = socket_address.port().as_uint16_t();
    changed = true;
  }

  FTL_DCHECK(net_address->addr.size() == socket_address.address().byte_count());
  if (std::memcmp(net_address->addr.data(), socket_address.address().as_bytes(),
                  net_address->addr.size()) != 0) {
    std::memcpy(net_address->addr.data(), socket_address.address().as_bytes(),
                net_address->addr.size());
    changed = true;
  }

  return changed;
}

}  // namespace mdns
}  // namespace netconnector
