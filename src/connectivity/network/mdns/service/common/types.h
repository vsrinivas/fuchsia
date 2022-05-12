// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_COMMON_TYPES_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_COMMON_TYPES_H_

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

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_COMMON_TYPES_H_
