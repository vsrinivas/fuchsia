// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/uuid/uuid.h"

#include <stddef.h>

#include <optional>
#include <ostream>

#include "src/lib/fxl/strings/string_printf.h"

#if defined(__Fuchsia__)
#include <zircon/syscalls.h>
#else
#include <algorithm>
#include <random>
#endif

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

void FillRandomly(RawUuid* raw) {
#if defined(__Fuchsia__)
  zx_cprng_draw(raw, kUuidSize);
#else
  std::random_device rd;
  auto* begin = reinterpret_cast<std::random_device::result_type*>(raw);
  auto* end = begin + kUuidSize / sizeof(std::random_device::result_type);
  std::generate(begin, end, std::ref(rd));
#endif
}

}  // namespace

Uuid Uuid::Generate() {
  // We generate a 128-bit (pseudo) random UUID in the form of version 4 as described
  // in RFC 4122, section 4.4.

  // Generate 16 random bytes.
  Uuid result;
  FillRandomly(&result.raw_);

  // Set the version field (bits 12 through 15 of |time_hi_and_version|) to 4.
  result.raw_.time_hi_and_version = (result.raw_.time_hi_and_version & 0x0fffu) | 0x4000u;

  // Set the reserved bits (bits 6 and 7) of |clock_seq_hi_and_reserved| to zero
  // and one, respectively.
  result.raw_.clock_seq_hi_and_reserved = (result.raw_.clock_seq_hi_and_reserved & 0x3fu) | 0x80u;

  // Return the UUID.
  return result;
}

std::optional<uuid::Uuid> Uuid::FromString(std::string_view uuid) {
  RawUuid guid;
  static constexpr int kUuidStringSize = 36;
  if (uuid.size() != kUuidStringSize) {
    // String can't be a valid UUID.
    return std::nullopt;
  }
  // sscanf() will parse extra dashes as negative numbers (e.g. -ab => "negative 0xab").
  // Check there's the expected number of dashes.
  int num_dashes = 0;
  for (char i : uuid) {
    if (i == '-') {
      num_dashes++;
    }
  }
  if (num_dashes != 4) {
    // We expect exactly 4 dashes.
    return std::nullopt;
  }
  int matched = sscanf(uuid.data(), "%8x-%4hx-%4hx-%2hhx%2hhx-%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx",
                       &guid.time_low, &guid.time_mid, &guid.time_hi_and_version,
                       &guid.clock_seq_hi_and_reserved, &guid.clock_seq_low, &guid.node[0],
                       &guid.node[1], &guid.node[2], &guid.node[3], &guid.node[4], &guid.node[5]);
  if (matched != 11) {
    // Didn't match all the fields in sscanf() above.
    return std::nullopt;
  }

  return uuid::Uuid(guid);
}

std::string Uuid::ToString() const {
  // Print the string.
  return fxl::StringPrintf("%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", raw_.time_low,
                           raw_.time_mid, raw_.time_hi_and_version, raw_.clock_seq_hi_and_reserved,
                           raw_.clock_seq_low, raw_.node[0], raw_.node[1], raw_.node[2],
                           raw_.node[3], raw_.node[4], raw_.node[5]);
}

std::ostream& operator<<(std::ostream& out, const Uuid& uuid) { return out << uuid.ToString(); }

std::string Generate() { return Uuid::Generate().ToString(); }

bool IsValid(const std::string& guid) { return IsValidInternal(guid, /*strict=*/false); }

bool IsValidOutputString(const std::string& guid) { return IsValidInternal(guid, /*strict=*/true); }

}  // namespace uuid
