// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FOSTR_HEX_DUMP_H_
#define LIB_FOSTR_HEX_DUMP_H_

#include <limits>
#include <vector>

#include "lib/fostr/indent.h"

namespace fostr {

// To insert a hex dump into an ostream, do this:
//      os << fostr::hex_dump(data, size);
// or this:
//      os << fostr::hex_dump(data, size, initial_address);
//
// |data| may be any raw pointer type, and |size| is in bytes. If
// |initial_address| isn't supplied or is max size_t, the actual address
// (|data|) is used. The address width is the smallest of 4, 8 and 16 that will
// accommodate the address values.
//
// The formatter used here follows the conventions described in indent.h. The
// formatter inserts an initial newline (because the first line of the dump is
// intended to appear on its own line), and the last line is not terminated.
// The indent manipulators are used, so the formatter will honor the current
// indentation level and 'indent by' value.
//
// 0000  54 68 69 73 20 69 73 20  61 6e 20 65 78 61 6d 70  This is an examp
// 0010  6c 65 20 6f 66 20 68 6f  77 20 61 20 68 65 78 20  le of how a hex
// 0020  64 75 6d 70 20 6c 6f 6f  6b 73 2e 00              dump looks..
//
// If |data| is null, "<null>" is inserted instead of a hex dump. Otherwise, if
// |size| is zero, "<zero bytes at [address]>" is inserted.

namespace internal {

struct HexDump {
  HexDump(const uint8_t* data, size_t size, intptr_t initial_address)
      : data_(data), size_(size), initial_address_(initial_address) {}

  const uint8_t* data_;
  size_t size_;
  intptr_t initial_address_;
};

std::ostream& operator<<(std::ostream& os, const HexDump& value);

}  // namespace internal

template <typename T>
internal::HexDump HexDump(
    const T* data, size_t size,
    intptr_t initial_address = std::numeric_limits<intptr_t>::max()) {
  return internal::HexDump(reinterpret_cast<const uint8_t*>(data), size,
                           initial_address);
}

inline internal::HexDump HexDump(const std::vector<uint8_t>& vector) {
  return internal::HexDump(vector.data(), vector.size(), 0);
}

}  // namespace fostr

#endif  // LIB_FOSTR_HEX_DUMP_H_
