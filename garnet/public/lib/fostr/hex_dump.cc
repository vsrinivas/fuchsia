// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fostr/hex_dump.h"

#include <iomanip>

namespace fostr {
namespace internal {

static constexpr size_t kBytesPerLine = 16;

std::ostream& operator<<(std::ostream& os, const HexDump& value) {
  const uint8_t* bytes = value.data_;
  size_t bytes_remaining = value.size_;
  intptr_t address = value.initial_address_;

  if (bytes == nullptr) {
    return os << "<null>";
  }

  if (address == std::numeric_limits<intptr_t>::max()) {
    address = reinterpret_cast<size_t>(bytes);
  }

  int address_width = 4;
  if (address + bytes_remaining > 0x100000000) {
    address_width = 16;
  } else if (address + bytes_remaining > 0x10000) {
    address_width = 8;
  }

  if (bytes_remaining == 0) {
    return os << "<zero bytes at " << std::hex << std::setw(address_width)
              << std::setfill('0') << address << std::setfill(' ') << std::dec
              << ">";
  }

  while (true) {
    os << NewLine << std::hex << std::setw(address_width) << std::setfill('0')
       << address << " ";

    std::string chars(kBytesPerLine, ' ');

    for (size_t i = 0; i < kBytesPerLine; ++i) {
      if (i == kBytesPerLine / 2) {
        os << " ";
      }

      if (i >= bytes_remaining) {
        os << "   ";
      } else {
        os << " " << std::setw(2) << std::setfill('0')
           << static_cast<uint16_t>(*bytes);
        if (*bytes >= ' ' && *bytes <= '~') {
          chars[i] = *bytes;
        } else {
          chars[i] = '.';
        }

        ++bytes;
      }
    }

    os << std::setfill(' ') << "  " << chars;

    if (bytes_remaining <= kBytesPerLine) {
      break;
    }

    address += kBytesPerLine;
    bytes_remaining -= kBytesPerLine;
  }

  return os << std::dec;
}

}  // namespace internal
}  // namespace fostr
