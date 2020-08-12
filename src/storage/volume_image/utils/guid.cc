// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/utils/guid.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <string>

namespace storage::volume_image {
namespace {

// GuidSection represents a sequence of bytes within a representation of a GUID,
// that treated as a unit. This unit may be represented as little/big endian
// independently of the platform.
//
// Offsets and lengths are defined with respect to the byte sequence.
struct GuidSection {
  // Returns the offset of the first element in the section.
  constexpr int8_t begin() const { return multiplier * (reversed ? start + length - 1 : start); }

  // Returns the offset of the last element in the section.
  constexpr int8_t end() const { return multiplier * (reversed ? start - 1 : start + length); }

  // Returns the distance between two consecutive elements in the section, measured in bytes.
  constexpr int8_t next() const { return multiplier * (reversed ? -1 : +1); }

  // Start of the section.
  uint8_t start;

  // End of the section.
  uint8_t length;

  // Whether this section should be iterated in a particular order.
  bool reversed;

  // Byte size of elements.
  uint8_t multiplier = 1;
};

// Defines the different sections of the GUID to match the following format:
//
// Byte-Format: {section_0}....{section_N}
// String Format: {String(section_0)}-....-{String(section_N)}
//
//  Example:
//    Byte-Format: {0xA0, 0xA1, 0xA2, 0xA3, 0xB0, 0xB1, 0xC0, 0xC1, 0xD0, 0xD1, 0xE0, 0xE1, 0xE2,
//                  0xE3, 0xE4, 0xE5}
//    String-Format: A3A2A1A0-B1B0-C1C0-D0D1-E0E1E2E3E4E5
constexpr std::array<GuidSection, 5> kGuidSections = {
    {
        // section: 0
        // Bytes: {0xA0, 0xA1, 0xA2, 0xA3}
        // String: A3A2A1A0
        {.start = 0, .length = 4, .reversed = true},
        // section: 1
        // Bytes: {0xB0, 0xB1}
        // String: B1B0
        {4, 2, true},
        // section: 2
        // Bytes: {0xC0, 0xC1}
        // String C1C0
        {6, 2, true},
        // section: 3
        // Bytes: {0xD0, 0xD1}
        // D0D1
        {8, 2, false},
        // section 4
        // Bytes: {0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5}
        // String E0E1E2E3E4E5
        {10, 6, false},
    },
};

// Separator used for string representation of the GUID.
constexpr char kSeparator = '-';

// Returns the Hex equivalent of |value|.
//
// Precondition:
//  * |value| is less than 16 or |F| + 1.
constexpr char GetHex(uint8_t value) {
  ZX_ASSERT(value < 16);
  constexpr std::string_view kHexTable = "0123456789ABCDEF";
  return kHexTable[value];
}

// Returns the numeric value of |hex|.
//
// Precondition:
//  * |hex| is either a digit(0-9) or [a-fA-F].
uint8_t GetValue(char hex) {
  constexpr uint8_t kHexAlphaOffset = 0xA;
  if (hex >= 'a') {
    return hex - 'a' + kHexAlphaOffset;
  }
  if (hex >= 'A') {
    return hex - 'A' + kHexAlphaOffset;
  }
  return hex - '0';
}

constexpr uint64_t kHighMask = 0xF0;
constexpr uint64_t kLowMask = 0x0F;

}  // namespace

fit::result<std::string, std::string> Guid::ToString(fbl::Span<const uint8_t> guid) {
  if (guid.size() != kGuidLength) {
    std::string error = "Input GUID size must be equal to |kGuidLength|. Input Size: ";
    error.append(std::to_string(guid.size())).append(".\n");
    return fit::error(error);
  }
  std::array<char, kGuidStrLength> out_guid;

  uint8_t current_section = 0;
  uint8_t current_byte = 0;

  for (auto section : kGuidSections) {
    section.multiplier = sizeof(uint8_t);
    const uint8_t* begin = std::begin(guid) + section.begin();
    const uint8_t* end = std::begin(guid) + section.end();

    for (const uint8_t* it = begin; it != end; it = it + section.next()) {
      uint8_t high = (kHighMask & *it) >> 4;
      uint8_t low = (kLowMask & *it);
      out_guid[2 * current_byte + current_section] = GetHex(high);
      out_guid[2 * current_byte + current_section + 1] = GetHex(low);
      current_byte++;
    }
    // We dont need a separator after last section.
    if (current_section < kGuidSections.size() - 1) {
      out_guid[2 * (section.start + section.length) + current_section] = kSeparator;
    }
    current_section++;
  }
  return fit::ok(std::string(out_guid.data(), out_guid.size()));
}

fit::result<std::array<uint8_t, kGuidLength>, std::string> Guid::FromString(
    fbl::Span<const char> guid) {
  if (guid.size() != kGuidStrLength) {
    std::string error = "Input GUID size must be equal to |kGuidStrLength|. Input Size: ";
    error.append(std::to_string(guid.size())).append(".\n");
    return fit::error(error);
  }
  std::array<uint8_t, kGuidLength> out_guid;

  uint64_t current_byte = 0;
  uint64_t current_section = 0;

  for (auto section : kGuidSections) {
    // We iterate 2 characters at a time.
    section.multiplier = kGuidCharactersPerByte * sizeof(char);
    const char* begin = std::begin(guid) + section.begin() + current_section;
    const char* end = std::begin(guid) + section.end() + current_section;

    for (const char* it = begin; it != end; it = it + section.next()) {
      uint8_t high = GetValue(*it);
      uint8_t low = GetValue(*(it + 1));
      out_guid[current_byte] = (high << 4) | low;
      current_byte++;
    }
    current_section++;
  }

  return fit::ok(out_guid);
}

}  // namespace storage::volume_image
