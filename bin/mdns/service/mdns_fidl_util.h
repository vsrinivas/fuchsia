// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/mdns/service/mdns.h"
#include "garnet/bin/mdns/service/socket_address.h"
#include <mdns/cpp/fidl.h>

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

  static void UpdateServiceInstance(
      const MdnsServiceInstancePtr& service_instance,
      const SocketAddress& v4_address,
      const SocketAddress& v6_address,
      const std::vector<std::string>& text);

  static netstack::SocketAddressPtr CreateSocketAddressIPv4(
      const IpAddress& ip_address);

  static netstack::SocketAddressPtr CreateSocketAddressIPv6(
      const IpAddress& ip_address);

  static netstack::SocketAddressPtr CreateSocketAddressIPv4(
      const SocketAddress& socket_address);

  static netstack::SocketAddressPtr CreateSocketAddressIPv6(
      const SocketAddress& socket_address);

  static IpAddress IpAddressFrom(const netstack::NetAddress* addr);

  static std::unique_ptr<Mdns::Publication> Convert(
      const MdnsPublicationPtr& publication_ptr);
};

}  // namespace mdns
