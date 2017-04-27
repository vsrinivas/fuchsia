// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/netconnector/services/mdns.fidl.h"
#include "apps/netconnector/src/socket_address.h"

namespace netconnector {
namespace mdns {

// mDNS utility functions relating to fidl types.
class MdnsFidlUtil {
 public:
  static const std::string kFuchsiaServiceName;

  static MdnsServiceInstancePtr CreateServiceInstance(
      const std::string& service_name,
      const std::string& instance_name,
      const SocketAddress& v4_address,
      const SocketAddress& v6_address,
      const std::vector<std::string>& text);

  static bool UpdateServiceInstance(
      const MdnsServiceInstancePtr& service_instance,
      const SocketAddress& v4_address,
      const SocketAddress& v6_address,
      const std::vector<std::string>& text);

  static network::NetAddressIPv4Ptr CreateNetAddressIPv4(
      const IpAddress& ip_address);

  static network::NetAddressIPv6Ptr CreateNetAddressIPv6(
      const IpAddress& ip_address);

  static network::NetAddressIPv4Ptr CreateNetAddressIPv4(
      const SocketAddress& socket_address);

  static network::NetAddressIPv6Ptr CreateNetAddressIPv6(
      const SocketAddress& socket_address);

  static bool UpdateNetAddressIPv4(
      const network::NetAddressIPv4Ptr& net_address,
      const SocketAddress& socket_address);

  static bool UpdateNetAddressIPv6(
      const network::NetAddressIPv6Ptr& net_address,
      const SocketAddress& socket_address);
};

}  // namespace mdns
}  // namespace netconnector
