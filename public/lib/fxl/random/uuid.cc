// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/random/uuid.h"

#include <stddef.h>
#include <stdint.h>

#include <string>

#include "lib/fxl/random/rand.h"
#include "lib/fxl/strings/string_printf.h"

namespace fxl {
namespace {

inline bool IsHexDigit(char c) {
  return (c >= '0' && c <= '9') ||
         (c >= 'A' && c <= 'F') ||
         (c >= 'a' && c <= 'f');
}

inline bool IsLowerHexDigit(char c) {
  return (c >= '0' && c <= '9') ||
         (c >= 'a' && c <= 'f');
}

bool IsValidUUIDInternal(const std::string& guid, bool strict) {
  const size_t kUUIDLength = 36U;
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

std::string GenerateUUID() {
  uint64_t bytes[2] = {fxl::RandUint64(), fxl::RandUint64()};

  // Set the UUID to version 4 as described in RFC 4122, section 4.4.
  // The format of UUID version 4 must be xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx,
  // where y is one of [8, 9, A, B].
  // Clear the version bits and set the version to 4:
  bytes[0] &= 0xffffffffffff0fffULL;
  bytes[0] |= 0x0000000000004000ULL;

  // Set the two most significant bits (bits 6 and 7) of the
  // clock_seq_hi_and_reserved to zero and one, respectively:
  bytes[1] &= 0x3fffffffffffffffULL;
  bytes[1] |= 0x8000000000000000ULL;

  return StringPrintf("%08x-%04x-%04x-%04x-%012llx",
                      static_cast<unsigned int>(bytes[0] >> 32),
                      static_cast<unsigned int>((bytes[0] >> 16) & 0x0000ffff),
                      static_cast<unsigned int>(bytes[0] & 0x0000ffff),
                      static_cast<unsigned int>(bytes[1] >> 48),
                      bytes[1] & 0x0000ffffffffffffULL);
}

bool IsValidUUID(const std::string& guid) {
  return IsValidUUIDInternal(guid, false /* strict */);
}

bool IsValidUUIDOutputString(const std::string& guid) {
  return IsValidUUIDInternal(guid, true /* strict */);
}

}  // namespace fxl
