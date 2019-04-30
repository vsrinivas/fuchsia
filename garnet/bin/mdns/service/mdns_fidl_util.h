// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MDNS_SERVICE_MDNS_FIDL_UTIL_H_
#define GARNET_BIN_MDNS_SERVICE_MDNS_FIDL_UTIL_H_

#include <fuchsia/net/mdns/cpp/fidl.h>

#include "garnet/bin/mdns/service/mdns.h"
#include "garnet/lib/inet/socket_address.h"

namespace mdns {

// mDNS utility functions relating to fidl types.
class MdnsFidlUtil {
 public:
  static const std::string kFuchsiaServiceName;

  static fuchsia::net::mdns::ServiceInstancePtr CreateServiceInstance(
      const std::string& service, const std::string& instance,
      const inet::SocketAddress& v4_address,
      const inet::SocketAddress& v6_address,
      const std::vector<std::string>& text);

  static void UpdateServiceInstance(
      const fuchsia::net::mdns::ServiceInstancePtr& service_instance,
      const inet::SocketAddress& v4_address,
      const inet::SocketAddress& v6_address,
      const std::vector<std::string>& text);

  static fuchsia::net::Ipv4Address CreateIpv4Address(
      const inet::IpAddress& ip_address);

  static fuchsia::net::Ipv6Address CreateIpv6Address(
      const inet::IpAddress& ip_address);

  static fuchsia::net::Endpoint CreateEndpointV4(
      const inet::SocketAddress& socket_address);

  static fuchsia::net::Endpoint CreateEndpointV6(
      const inet::SocketAddress& socket_address);

  static inet::IpAddress IpAddressFrom(const fuchsia::net::IpAddress* addr);

  static std::unique_ptr<Mdns::Publication> Convert(
      const fuchsia::net::mdns::PublicationPtr& publication_ptr);
};

}  // namespace mdns

#endif  // GARNET_BIN_MDNS_SERVICE_MDNS_FIDL_UTIL_H_
