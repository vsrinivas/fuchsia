// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/uuid/uuid.h"

#include <stddef.h>
#include <stdint.h>
#include <zircon/syscalls.h>

#include <string>

#include "src/lib/fxl/strings/string_printf.h"

namespace uuid {
namespace {

inline bool IsHexDigit(char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

inline bool IsLowerHexDigit(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }

bool IsValidInternal(const std::string& guid, bool strict) {
  constexpr size_t kUUIDLength = 36U;
  if (guid.length() != kUUIDLength)
    return false;
  for (size_t i = 0; i < guid.length(); ++i) {
    char current = guid[i];
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (current != '-')
        return false;
    } else {
      if ((strict && !IsLowerHexDigit(current)) || !IsHexDigit(current))
        return false;
    }
  }
  return true;
}

}  // namespace

Uuid Uuid::Generate() {
  // We generate a 128-bit (pseudo) random UUID in the form of version 4 as described
  // in RFC 4122, section 4.4.

  // Generate 16 random bytes.
  Uuid result;
  zx_cprng_draw(&result.raw_, sizeof(result));

  // Set the version field (bits 12 through 15 of |time_hi_and_version|) to 4.
  result.raw_.time_hi_and_version = (result.raw_.time_hi_and_version & 0x0fffu) | 0x4000u;

  // Set the reserved bits (bits 6 and 7) of |clock_seq_hi_and_reserved| to zero
  // and one, respectively.
  result.raw_.clock_seq_hi_and_reserved = (result.raw_.clock_seq_hi_and_reserved & 0x3fu) | 0x80u;

  // Return the UUID.
  return result;
}

std::string Uuid::ToString() const {
  // Print the string.
  return fxl::StringPrintf("%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", raw_.time_low,
                           raw_.time_mid, raw_.time_hi_and_version, raw_.clock_seq_hi_and_reserved,
                           raw_.clock_seq_low, raw_.node[0], raw_.node[1], raw_.node[2],
                           raw_.node[3], raw_.node[4], raw_.node[5]);
}

std::string Generate() { return Uuid::Generate().ToString(); }

bool IsValid(const std::string& guid) { return IsValidInternal(guid, /*strict=*/false); }

bool IsValidOutputString(const std::string& guid) { return IsValidInternal(guid, /*strict=*/true); }

}  // namespace uuid
