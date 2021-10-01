// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_MDNS_FIDL_UTIL_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_MDNS_FIDL_UTIL_H_

#include <fuchsia/net/interfaces/cpp/fidl.h>
#include <fuchsia/net/mdns/cpp/fidl.h>

#include "src/connectivity/network/mdns/service/mdns.h"
#include "src/lib/inet/socket_address.h"

namespace mdns {

// mDNS utility functions relating to fidl types.
class MdnsFidlUtil {
 public:
  static const std::string kFuchsiaServiceName;

  static fuchsia::net::Ipv4Address CreateIpv4Address(const inet::IpAddress& ip_address);

  static fuchsia::net::Ipv6Address CreateIpv6Address(const inet::IpAddress& ip_address);

  static fuchsia::net::IpAddress CreateIpAddress(const inet::IpAddress& ip_address);

  static fuchsia::net::Ipv4SocketAddress CreateSocketAddressV4(
      const inet::SocketAddress& socket_address);

  static fuchsia::net::Ipv6SocketAddress CreateSocketAddressV6(
      const inet::SocketAddress& socket_address);

  static inet::IpAddress IpAddressFrom(const fuchsia::net::IpAddress& addr);

  static std::unique_ptr<Mdns::Publication> Convert(
      const fuchsia::net::mdns::PublicationPtr& publication_ptr);

  static std::vector<fuchsia::net::IpAddress> Convert(
      const std::vector<inet::SocketAddress>& addresses);

  static fuchsia::net::mdns::ResourceType Convert(DnsType type);

  static void FillServiceInstance(fuchsia::net::mdns::ServiceInstance* service_instance,
                                  const std::string& service, const std::string& instance,
                                  const inet::SocketAddress& v4_address,
                                  const inet::SocketAddress& v6_address,
                                  const std::vector<std::string>& text, uint16_t srv_priority,
                                  uint16_t srv_weight, const std::string& target);
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_MDNS_FIDL_UTIL_H_
