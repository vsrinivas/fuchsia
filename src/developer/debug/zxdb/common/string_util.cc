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
  if (i <= std::numeric_limits<uint64_t>::max())
    return to_hex_string(static_cast<uint64_t>(i), digits, include_prefix);

  // Compute the padding to apply to just the high 64 bits. The low 64 bits will always have 16.
  int high_digits = 0;
  if (digits > 16)
    high_digits = digits - 16;

  std::string result = to_hex_string(static_cast<uint64_t>(i >> 64), high_digits, include_prefix);
  result += to_hex_string(static_cast<uint64_t>(i), 16, false);
  return result;
}

template <typename T>
std::string to_bin_string(T value, int digits, bool include_prefix, char byte_separator) {
  std::ostringstream out;
  if (include_prefix)
    out << "0b";

  // When no padding is requested, always output at least one digit.
  if (digits == 0)
    digits = 1;

  int high_bit = sizeof(T) * 8 - 1;              // Largest bit of the input type (0-based).
  bool written_digit = false;                    // Set when we've written any digit.
  int cur_bit = std::max(digits - 1, high_bit);  // 0-based bit index (counting from low bit).

  // Computes whether a byte separator is needed at cur_bit.
  auto needs_separator = [&]() {
    return written_digit &&         // Don't do a leading separator.
           byte_separator &&        // Separator requested.
           (cur_bit + 1) % 8 == 0;  // Byte boundary.
  };

  // Left 0-padding for padding beyond the range of the input type.
  while (cur_bit > high_bit) {
    if (needs_separator())
      out << byte_separator;
    out << '0';

    written_digit = true;
    cur_bit--;
  }

  while (cur_bit >= 0) {
    if (needs_separator())
      out << byte_separator;

    bool bit_value = value & (static_cast<T>(1) << cur_bit);
    if (bit_value || written_digit || (cur_bit + 1) <= digits) {
      out << (bit_value ? '1' : '0');
      written_digit = true;
    }

    cur_bit--;
  }

  return out.str();
}

template std::string to_bin_string<int8_t>(int8_t i, int digits, bool include_prefix,
                                           char byte_separator);
template std::string to_bin_string<uint8_t>(uint8_t i, int digits, bool include_prefix,
                                            char byte_separator);
template std::string to_bin_string<int16_t>(int16_t i, int digits, bool include_prefix,
                                            char byte_separator);
template std::string to_bin_string<uint16_t>(uint16_t i, int digits, bool include_prefix,
                                             char byte_separator);
template std::string to_bin_string<int32_t>(int32_t i, int digits, bool include_prefix,
                                            char byte_separator);
template std::string to_bin_string<uint32_t>(uint32_t i, int digits, bool include_prefix,
                                             char byte_separator);
template std::string to_bin_string<int64_t>(int64_t i, int digits, bool include_prefix,
                                            char byte_separator);
template std::string to_bin_string<uint64_t>(uint64_t i, int digits, bool include_prefix,
                                             char byte_separator);
template std::string to_bin_string<int128_t>(int128_t i, int digits, bool include_prefix,
                                             char byte_separator);
template std::string to_bin_string<uint128_t>(uint128_t i, int digits, bool include_prefix,
                                              char byte_separator);

}  // namespace zxdb
