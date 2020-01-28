// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/common/string_util.h"

#include <inttypes.h>

#include <iomanip>
#include <sstream>

namespace zxdb {

namespace {

std::string DoToHexString(uint64_t value, int digits, bool include_prefix) {
  std::ostringstream out;
  if (include_prefix)
    out << "0x";

  if (digits)
    out << std::setw(digits) << std::setfill('0');

  out << std::hex << value;
  return out.str();
}

}  // namespace

bool StringBeginsWith(std::string_view str, std::string_view begins_with) {
  if (begins_with.size() > str.size())
    return false;

  std::string_view source = str.substr(0, begins_with.size());
  return source == begins_with;
}

bool StringEndsWith(std::string_view str, std::string_view ends_with) {
  if (ends_with.size() > str.size())
    return false;

  std::string_view source = str.substr(str.size() - ends_with.size(), ends_with.size());
  return source == ends_with;
}

// Cast signed numbers to their unsigned variant before converting to 64-bit to avoid sign
// extension.
std::string to_hex_string(int8_t i, int digits, bool include_prefix) {
  return DoToHexString(static_cast<uint8_t>(i), digits, include_prefix);
}
std::string to_hex_string(uint8_t i, int digits, bool include_prefix) {
  return DoToHexString(i, digits, include_prefix);
}
std::string to_hex_string(int16_t i, int digits, bool include_prefix) {
  return DoToHexString(static_cast<uint16_t>(i), digits, include_prefix);
}
std::string to_hex_string(uint16_t i, int digits, bool include_prefix) {
  return DoToHexString(i, digits, include_prefix);
}
std::string to_hex_string(int32_t i, int digits, bool include_prefix) {
  return DoToHexString(static_cast<uint32_t>(i), digits, include_prefix);
}
std::string to_hex_string(uint32_t i, int digits, bool include_prefix) {
  return DoToHexString(i, digits, include_prefix);
}
std::string to_hex_string(int64_t i, int digits, bool include_prefix) {
  return DoToHexString(static_cast<uint64_t>(i), digits, include_prefix);
}
std::string to_hex_string(uint64_t i, int digits, bool include_prefix) {
  return DoToHexString(i, digits, include_prefix);
}

// Format the 128-bit numbers as two 64-bit ones.
std::string to_hex_string(int128_t i, int digits, bool include_prefix) {
  return to_hex_string(static_cast<uint128_t>(i), digits, include_prefix);
}
std::string to_hex_string(uint128_t i, int digits, bool include_prefix) {
  // Compute the padding to apply to just the high 64 bits. The low 64 bits will always have 16.
  int high_digits = 0;
  if (digits > 16)
    high_digits = digits - 16;

  std::string result = to_hex_string(static_cast<uint64_t>(i >> 64), high_digits, include_prefix);
  result += to_hex_string(static_cast<uint64_t>(i), 16, false);
  return result;
}

}  // namespace zxdb
