// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/perr_destination_parser.h>

namespace wlan {
namespace common {

PerrDestinationParser::PerrDestinationParser(Span<const uint8_t> bytes)
    : reader_(bytes) {}

std::optional<ParsedPerrDestination> PerrDestinationParser::Next() {
  ParsedPerrDestination ret = {};
  ret.header = reader_.Read<PerrPerDestinationHeader>();
  if (ret.header == nullptr) {
    return {};
  }

  if (ret.header->flags.addr_ext()) {
    ret.ext_addr = reader_.Read<MacAddr>();
    if (ret.ext_addr == nullptr) {
      incomplete_read_ = true;
      return {};
    }
  }

  ret.tail = reader_.Read<PerrPerDestinationTail>();
  if (ret.tail == nullptr) {
    incomplete_read_ = true;
    return {};
  }

  return {ret};
}

bool PerrDestinationParser::ExtraBytesLeft() const {
  return incomplete_read_ || reader_.RemainingBytes();
}

}  // namespace common
}  // namespace wlan
