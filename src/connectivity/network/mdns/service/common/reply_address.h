// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_COMMON_REPLY_ADDRESS_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_COMMON_REPLY_ADDRESS_H_

#include <arpa/inet.h>
#include <sys/socket.h>

#include "src/connectivity/network/mdns/service/common/mdns_addresses.h"
#include "src/connectivity/network/mdns/service/common/types.h"
#include "src/lib/inet/socket_address.h"

namespace mdns {

// SocketAddress with interface address.
class ReplyAddress {
 public:
  // Returns a placeholder multicast address. When used to express a destination for questions,
  // resources or messages, a placeholder multicast address indicate the item should be directed to
  // the appropriate multicast address (V4 or V6) on each interface that meets the qualifications
  // represented by |media| and |ip_versions|.
  static ReplyAddress Multicast(Media media, IpVersions ip_versions) {
    return ReplyAddress(MdnsAddresses::v4_multicast(), inet::IpAddress(), media, ip_versions);
  }

  // Creates a reply address with an invalid socket address and interface.
  ReplyAddress();

  // Creates a reply address from an |SocketAddress| and an interface |IpAddress|.
  ReplyAddress(const inet::SocketAddress& socket_address, const inet::IpAddress& interface_address,
               Media media, IpVersions ip_versions);

  // Creates a reply address from an |sockaddr_storage| struct and an interface
  // |IpAddress|.
  ReplyAddress(const sockaddr_storage& socket_address, const inet::IpAddress& interface_address,
               Media media, IpVersions ip_versions);

  const inet::SocketAddress& socket_address() const { return socket_address_; }

  const inet::IpAddress& interface_address() const { return interface_address_; }

  // Determines whether this |ReplyAddress| is a multicast placeholder as produced by the
  // |Multicast| static method of this class. The V4 multicast address is used to identify such
  // placeholder addresses.
  bool is_multicast_placeholder() const { return socket_address_ == MdnsAddresses::v4_multicast(); }

  // For unicast reply addresses, this field is set to |kWired| or |kWireless| to describe the
  // interface. For multicast reply addresses, this field is set to |kWired| to multicast via
  // wired interfaces only, |kWireless| to multicast via wireless interfaces only, or |kBoth|
  // to multicast via both wired and wireless interfaces.
  Media media() const { return media_; }

  // For unicast reply addresses, this field is set to |kV4| or |kV6| to describe the
  // interface. For multicast reply addresses, this field is set to |kV4| to multicast via
  // V4 interfaces only, |kV6| to multicast via V6 interfaces only, or |kBoth|
  // to multicast via both V4 and V6 interfaces.
  IpVersions ip_versions() const { return ip_versions_; }

  // Determines if this |ReplyAddress| matches the specified |Media|.
  bool Matches(Media media) const { return media == Media::kBoth || media_ == media; }

  // Determines if this |ReplyAddress| matches the specified |IpVersions|.
  bool Matches(IpVersions ip_versions) const {
    return ip_versions == IpVersions::kBoth || ip_versions_ == ip_versions;
  }

  bool operator==(const ReplyAddress& other) const {
    return socket_address_ == other.socket_address() &&
           interface_address_ == other.interface_address() && media_ == other.media_ &&
           ip_versions_ == other.ip_versions_;
  }

  bool operator!=(const ReplyAddress& other) const { return !(*this == other); }

 private:
  inet::SocketAddress socket_address_;
  inet::IpAddress interface_address_;
  Media media_;
  IpVersions ip_versions_;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_COMMON_REPLY_ADDRESS_H_
