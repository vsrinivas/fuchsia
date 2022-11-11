// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_UUID_UUID_H_
#define SRC_LIB_UUID_UUID_H_

#include <array>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>

namespace uuid {

// Number of bytes in a UUID.
constexpr size_t kUuidSize = 16;

// The internal components of a UUID.
//
// c.f., RFC 4122 Section 4.1.2.
//
// Most users should the |Uuid| type directly, below.
struct RawUuid {
  uint32_t time_low;
  uint16_t time_mid;
  uint16_t time_hi_and_version;
  uint8_t clock_seq_hi_and_reserved;
  uint8_t clock_seq_low;
  uint8_t node[6];
};

static_assert(sizeof(RawUuid) == kUuidSize);

// A Universally Unique Identifier (UUID) or, equivalently, a Globally Unique
// Identifier (GUID) is a 128-bit identifier. UUIDs can be independently
// generated while having strong guarantees that no two generated UUIDs will
// have the same value.
//
// The format and algorithm for generating UUID is described in RFC 4122.
class Uuid {
 public:
  // Generate the empty UUID ("00000000-0000-0000-0000-000000000000").
  constexpr Uuid() : raw_{} {}

  // Generate a UUID from the given 16-byte array.
  //
  // We assume that the buffer is stored in host-native endian order.
  explicit Uuid(const uint8_t* buffer) { memcpy(&raw_, buffer, kUuidSize); }

  // Generate a UUID from a RawUuid.
  constexpr explicit Uuid(RawUuid raw) : raw_(raw) {}

  // Generate a UUID from the given 16 bytes.
  //
  // This constructor can be used to generate compile-time constant UUIDs:
  //
  //   constexpr Uuid kMyUuid = {0x01, 0x02, 0x03, 0x04, 0x04, 0x05, 0x06, 0x07,
  //                             0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
  //
  // The input bytes are assumed to be in host-native endian format.
  constexpr Uuid(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4, uint8_t b5, uint8_t b6,
                 uint8_t b7, uint8_t b8, uint8_t b9, uint8_t b10, uint8_t b11, uint8_t b12,
                 uint8_t b13, uint8_t b14, uint8_t b15)
      : bytes_{b0, b1, b2, b3, b4, b5, b6, b7, b8, b9, b10, b11, b12, b13, b14, b15} {}

  // Generate a new v4 UUID.
  //
  // This method generates a version 4 (random) UUID, using 122 bits of entropy
  // provided by the kernel. The algorithm is described in RFC 4122,
  // section 4.4.
  static Uuid Generate();

  // Parse a UUID of the format returned by ToString.
  // Returns std::nullopt if the string was invalid.
  static std::optional<Uuid> FromString(std::string_view uuid);

  // Generate a string representation of this UUID. The returned string will
  // be of the form:
  //
  //   00112233-4455-6677-8899-aabbccddeeff
  //
  std::string ToString() const;

  // Raw bytes of the UUID, in host-endian format.
  const uint8_t* bytes() const { return bytes_; }

  // Raw fields of the UUID.
  const RawUuid& raw() const { return raw_; }

  uint8_t* begin() { return bytes_; }
  uint8_t* end() { return bytes_ + kUuidSize; }

  const uint8_t* cbegin() const { return bytes_; }
  const uint8_t* cend() const { return bytes_ + kUuidSize; }

 private:
  union {
    RawUuid raw_;
    uint8_t bytes_[kUuidSize];
  };
};

static_assert(sizeof(Uuid) == kUuidSize);

// Equality / inequality.
inline bool operator==(const Uuid& a, const Uuid& b) {
  return memcmp(a.bytes(), b.bytes(), kUuidSize) == 0;
}
inline bool operator!=(const Uuid& a, const Uuid& b) { return !(a == b); }

// Writes Uuid.ToString() to the given stream.
std::ostream& operator<<(std::ostream& out, const Uuid& uuid);

// Generate a 128-bit (pseudo) random UUID in the form of version 4 as described
// in RFC 4122, section 4.4.
// The format of UUID version 4 must be xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx,
// where y is one of [8, 9, A, B].
// The hexadecimal values "a" through "f" are output as lower case characters.
std::string Generate();

// Returns true if the input string conforms to the version 4 UUID format.
// Note that this does NOT check if the hexadecimal values "a" through "f"
// are in lower case characters, as Version 4 RFC says they're
// case insensitive. (Use IsValidOutputString for checking if the
// given string is valid output string)
bool IsValid(const std::string& guid);

// Returns true if the input string is valid version 4 UUID output string.
// This also checks if the hexadecimal values "a" through "f" are in lower
// case characters.
bool IsValidOutputString(const std::string& guid);

}  // namespace uuid

#endif  // SRC_LIB_UUID_UUID_H_
