// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/common/formatters.h"

#include <ostream>

namespace mdns {

std::ostream& operator<<(std::ostream& os, const Media& value) {
  switch (value) {
    case Media::kWired:
      return os << "wired";
    case Media::kWireless:
      return os << "wireless";
    case Media::kBoth:
      return os << "both";
  }
}

std::ostream& operator<<(std::ostream& os, const IpVersions& value) {
  switch (value) {
    case IpVersions::kV4:
      return os << "IPv4";
    case IpVersions::kV6:
      return os << "IPv6";
    case IpVersions::kBoth:
      return os << "IPv4/v6";
  }
}

std::ostream& operator<<(std::ostream& os, const ReplyAddress& value) {
  if (!value.socket_address().is_valid()) {
    return os << "<invalid>";
  }

  if (value.is_multicast_placeholder()) {
    return os << "mulitcast_placeholder " << value.media() << " " << value.ip_versions();
  }

  return os << value.socket_address() << " interface " << value.interface_address() << ""
            << value.media() << " " << value.ip_versions();
}

std::ostream& operator<<(std::ostream& os, MdnsResourceSection value) {
  switch (value) {
    case MdnsResourceSection::kAnswer:
      return os << "answer";
    case MdnsResourceSection::kAuthority:
      return os << "authority";
    case MdnsResourceSection::kAdditional:
      return os << "additional";
    case MdnsResourceSection::kExpired:
      return os << "EXPIRED";
  }
}

}  // namespace mdns
