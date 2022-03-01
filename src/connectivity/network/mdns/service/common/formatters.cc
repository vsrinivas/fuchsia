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

std::ostream& operator<<(std::ostream& os, const ReplyAddress& value) {
  if (!value.socket_address().is_valid()) {
    return os << "<invalid>";
  }

  return os << value.socket_address() << " interface " << value.interface_address() << " media "
            << value.media();
}

}  // namespace mdns
