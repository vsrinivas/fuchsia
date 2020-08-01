// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>

#include <cstdint>
#include <cstring>

zx_status_t fidl_validate_string(const char* data, uint64_t size) {
  if (!data) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (size > FIDL_MAX_SIZE) {
    return ZX_ERR_INVALID_ARGS;
  }

  uint64_t pos = 0;
  // Fast path:
  // Read ASCII bytes in 8-byte chunks until a non-ASCII byte is encountered.
  for (; pos < (size & ~7); pos += 8) {
    uint64_t val;
    memcpy(&val, &data[pos], 8);
    if ((val & 0x8080'8080'8080'8080) != 0) {
      break;
    }
  }

  while (pos < size) {
    uint64_t next_pos = 0;
    uint32_t code_point = 0;

    // The following comparison relies on treating the byte as if it was an
    // unsigned 8-bit value. However, both signed and unsigned char are allowed
    // in the C++ spec, with x64 choosing signed, and arm64 choosing unsigned.
    // We therefore force the byte to be treated as unsigned, since we cannot
    // rely on the default.
    unsigned char byte = data[pos];
    if (byte < 0b10000000) {  // Matches 0b1xxx xxxx
      pos++;
      continue;
    }

    if (byte < 0b11100000) {  // Matches 0b110x xxxx
      next_pos = pos + 2;
      if (next_pos > size) {
        return ZX_ERR_INVALID_ARGS;
      }
      if ((data[pos + 1] & 0b11000000) != 0b10000000) {
        return ZX_ERR_INVALID_ARGS;
      }
      // range check
      code_point = (byte & 0b00011111) << 6 | (data[pos + 1] & 0b00111111);
      if (code_point < 0x80 || 0x7ff < code_point) {
        return ZX_ERR_INVALID_ARGS;
      }
    } else if (byte < 0b11110000) {  // Matches 0b1110 xxxx
      next_pos = pos + 3;
      if (next_pos > size) {
        return ZX_ERR_INVALID_ARGS;
      }
      if ((data[pos + 1] & 0b11000000) != 0b10000000) {
        return ZX_ERR_INVALID_ARGS;
      }
      if ((data[pos + 2] & 0b11000000) != 0b10000000) {
        return ZX_ERR_INVALID_ARGS;
      }
      // range check
      code_point = (byte & 0b00001111) << 12 | (data[pos + 1] & 0b00111111) << 6 |
                   (data[pos + 2] & 0b00111111);
      if (code_point < 0x800 || 0xffff < code_point ||
          (0xd7ff < code_point && code_point < 0xe000)) {
        return ZX_ERR_INVALID_ARGS;
      }
    } else {  // Matches 0b1111 xxxx
      next_pos = pos + 4;
      if (next_pos > size) {
        return ZX_ERR_INVALID_ARGS;
      }
      if ((data[pos + 1] & 0b11000000) != 0b10000000) {
        return ZX_ERR_INVALID_ARGS;
      }
      if ((data[pos + 2] & 0b11000000) != 0b10000000) {
        return ZX_ERR_INVALID_ARGS;
      }
      if ((data[pos + 3] & 0b11000000) != 0b10000000) {
        return ZX_ERR_INVALID_ARGS;
      }
      // range check
      code_point = (byte & 0b00000111) << 18 | (data[pos + 1] & 0b00111111) << 12 |
                   (data[pos + 2] & 0b00111111) << 6 | (data[pos + 3] & 0b00111111);
      if (code_point < 0xffff || 0x10ffff < code_point) {
        return ZX_ERR_INVALID_ARGS;
      }
    }
    pos = next_pos;
  }
  return ZX_OK;
}
