// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>
#include <lib/stdcompat/bit.h>

#include <cstdint>
#include <cstring>

namespace {

bool ValidateFullUtf8(const char* data, uint64_t pos, const uint64_t size) {
  // Inclusive range check
  auto is_in_range = [](uint8_t byte, uint8_t lo, uint8_t hi) { return lo <= byte && byte <= hi; };

  // The following comparisons rely on treating bytes as if they are unsigned 8-bit values.
  // However, both signed and unsigned char are allowed in the C++ spec, with x64 choosing signed
  // and arm64 choosing unsigned. We therefore force the byte to be treated as unsigned, since we
  // cannot rely on the default.
  const uint8_t* str = reinterpret_cast<const uint8_t*>(data);
  static_assert(sizeof(char) == sizeof(uint8_t), "char and uint8_t are not the same size!");

  while (pos < size) {
    // Table from https://datatracker.ietf.org/doc/html/rfc3629#section-4
    //
    // UTF8-1      = %x00-7F
    // UTF8-2      = %xC2-DF UTF8-tail
    // UTF8-3      = %xE0 %xA0-BF UTF8-tail / %xE1-EC 2( UTF8-tail ) /
    //               %xED %x80-9F UTF8-tail / %xEE-EF 2( UTF8-tail )
    // UTF8-4      = %xF0 %x90-BF 2( UTF8-tail ) / %xF1-F3 3( UTF8-tail ) /
    //               %xF4 %x80-8F 2( UTF8-tail )
    // UTF8-tail   = %x80-BF

    const uint64_t remaining_size = size - pos;

    if (str[pos] <= 0x7F) {
      // UTF8-1      = %x00-7F
      pos += 1;
    } else if (is_in_range(str[pos], 0xC2, 0xDF)) /* %xC2-DF */ {
      // UTF8-2      = %xC2-DF UTF8-tail
      if (remaining_size < 2) {
        return false;
      }
      if ((str[pos + 1] & 0b11000000) != 0b10000000) {
        // Not followed by continuation character.
        return false;
      }

      pos += 2;
    } else if (is_in_range(str[pos], 0xE0, 0xEF)) /* %xE0-EF */ {
      // UTF8-3      = %xE0 %xA0-BF UTF8-tail / %xE1-EC 2( UTF8-tail ) /
      //               %xED %x80-9F UTF8-tail / %xEE-EF 2( UTF8-tail )
      if (remaining_size < 3) {
        return false;
      }

      uint16_t continuations;
      memcpy(&continuations, &str[pos + 1], sizeof(continuations));
      if ((continuations & 0b11000000'11000000) != 0b10000000'10000000) {
        // Not followed by continuation characters.
        return false;
      }
      if (str[pos] == 0xE0 && !is_in_range(str[pos + 1], 0xA0, 0xBF)) {
        // First byte is %xE0 but second byte is not in range %xA0-BF.
        return false;
      }
      if (str[pos] == 0xED && !is_in_range(str[pos + 1], 0x80, 0x9F)) {
        // First byte is %xED but second byte is not in range %x80-9F.
        return false;
      }

      pos += 3;
    } else if (is_in_range(str[pos], 0xF0, 0xF4)) /* %xF0-F4 */ {
      // UTF8-4      = %xF0 %x90-BF 2( UTF8-tail ) / %xF1-F3 3( UTF8-tail ) /
      //               %xF4 %x80-8F 2( UTF8-tail )
      if (remaining_size < 4) {
        return false;
      }

      // Note: don't forget about endianness here!
      uint32_t code_point;
      memcpy(&code_point, &str[pos], sizeof(code_point));
      if constexpr (cpp20::endian::native == cpp20::endian::big) {
        if ((code_point & 0b11000000'11000000'11000000) != 0b10000000'10000000'10000000) {
          // Not followed by continuation characters.
          return false;
        }
      } else {
        if ((code_point & 0b11000000'11000000'11000000'00000000) !=
            0b10000000'10000000'10000000'00000000) {
          // Not followed by continuation characters.
          return false;
        }
      }

      if (str[pos] == 0xF0 && !is_in_range(str[pos + 1], 0x90, 0xBF)) {
        // First byte is %xF0 but second byte is not in range %x90-BF.
        return false;
      }
      if (str[pos] == 0xF4 && !is_in_range(str[pos + 1], 0x80, 0x8F)) {
        // First byte is %xF4 but second byte is not in range %x80-8F.
        return false;
      }

      pos += 4;
    } else {
      return false;
    }
  }

  return true;
}

}  // namespace

bool fidl_validate_string(const char* data, const uint64_t size) {
  if (data == nullptr) {
    return false;
  }
  if (size > FIDL_MAX_SIZE) {
    return false;
  }

  uint64_t pos = 0;

  // Fast path: read ASCII bytes in 8-byte chunks until a non-ASCII byte is encountered.
  for (; pos < (size & ~7); pos += 8) {
    uint64_t val;
    memcpy(&val, &data[pos], sizeof(val));
    if ((val & 0x8080'8080'8080'8080) != 0) {
      return ValidateFullUtf8(data, pos, size);
    }
  }

  // Fast path: drain loop for remaining chunk of ASCII (< 8-byte chunk)
  for (; pos < size; ++pos) {
    if ((data[pos] & 0x80) != 0) {
      return ValidateFullUtf8(data, pos, size);
    }
  }

  return true;
}
