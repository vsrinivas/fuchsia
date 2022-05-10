// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_COMMON_TYPES_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_COMMON_TYPES_H_

#include "src/lib/inet/ip_address.h"

namespace mdns {

enum class Media { kWired, kWireless, kBoth };

enum class IpVersions { kV4, kV6, kBoth };

enum class PublicationCause {
  kAnnouncement,
  kQueryMulticastResponse,
  kQueryUnicastResponse,
};

// kExpired is used when distributing resource expirations. It's not a real
// resource section.
enum class MdnsResourceSection { kAnswer, kAuthority, kAdditional, kExpired };

class HostAddress {
 public:
  struct Hash {
    std::size_t operator()(const HostAddress& value) const noexcept {
      return std::hash<inet::IpAddress>{}(value.address()) ^
             (std::hash<uint32_t>{}(value.interface_id()) << 1) ^
             (std::hash<int64_t>{}(value.ttl().get()) << 2);
    }
  };

  HostAddress() = default;

  HostAddress(inet::IpAddress address, uint32_t interface_id, zx::duration ttl)
      : address_(address), interface_id_(interface_id), ttl_(ttl) {}

  inet::IpAddress address() const { return address_; }

  uint32_t interface_id() const { return interface_id_; }

  zx::duration ttl() const { return ttl_; }

  bool operator==(const HostAddress& other) const {
    return address_ == other.address() && interface_id_ == other.interface_id() &&
           ttl_ == other.ttl();
  }

 private:
  inet::IpAddress address_;
  uint32_t interface_id_;
  zx::duration ttl_;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_COMMON_TYPES_H_
