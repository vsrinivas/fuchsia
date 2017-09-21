// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <unordered_set>

#include "garnet/drivers/bluetooth/lib/common/uint128.h"

namespace bluetooth {
namespace common {

class ByteBuffer;
class MutableByteBuffer;

// Represents a 128-bit Bluetooth UUID. This class allows UUID values to be constructed in the
// official Bluetooth 16-bit, 32-bit, and 128-bit formats and to be compared against any other
// Bluetooth UUID.
class UUID final {
 public:
  // Constructs a UUID from |bytes|. |bytes| should contain a 16-, 32-, or 128-bit UUID in
  // little-endian byte order. Returns false if |bytes| contains an unsupported size.
  static bool FromBytes(const common::ByteBuffer& bytes, UUID* out_uuid);

  explicit UUID(const UInt128& uuid128);
  explicit UUID(uint16_t uuid16);
  explicit UUID(uint32_t uuid32);

  // The default constructor initializes all values to zero.
  UUID();

  // Equality operators.
  bool operator==(const UUID& uuid) const;
  bool operator==(uint16_t uuid16) const;
  bool operator==(uint32_t uuid32) const;
  bool operator==(const UInt128& uuid128) const;
  bool operator!=(const UUID& uuid) const { return !(*this == uuid); }
  bool operator!=(uint16_t uuid16) const { return !(*this == uuid16); }
  bool operator!=(uint32_t uuid32) const { return !(*this == uuid32); }
  bool operator!=(const UInt128& uuid128) const { return !(*this == uuid128); }

  // Compares a UUID with the contents of a raw buffer in little-endian byte order. This is useful
  // for making a direct comparison with UUIDs received over PDUs. Returns false if |bytes| has an
  // unaccepted size; the only accepted sizes for are 2, 4, and 16 for 16-bit, 32-bit, and 128-bit
  // formats, respectively.
  bool CompareBytes(const common::ByteBuffer& bytes) const;

  // Returns the underlying value in little-endian byte order.
  const UInt128& value() const { return value_; }

  // Returns a string representation of this UUID in the following format:
  //
  //   xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
  //
  // where x is one of the alphanumeric characters in the string 0123456789abcdef.
  std::string ToString() const;

  // Returns the number of bytes required to store this UUID.
  size_t CompactSize() const;

  // Writes a representation of this UUID to |buffer|.  Returns the number of
  // bytes used. there must be enough space in |buffer| to store |compact_size()|
  // bytes.
  size_t ToBytes(common::MutableByteBuffer* buffer) const;

  // Returns a hash of this UUID.
  std::size_t Hash() const;

 private:
  // We store the type that this was initialized with to allow quick comparison with short Bluetooth
  // SIG UUIDs.
  enum class Type : uint8_t {
    k16Bit,
    k32Bit,
    k128Bit,
  };

  // If a quick conversion is possible, these return the 16 or 32 bit values of the UUID in host
  // byte order.
  uint16_t ValueAs16Bit() const;
  uint32_t ValueAs32Bit() const;

  Type type_;
  UInt128 value_;
};

// Returns true if the given |uuid_string| contains a valid UUID in the following format:
//
//   xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
//
// where x is one of the alphanumeric characters in the string 0123456789abcdefABCDEF.
bool IsStringValidUuid(const std::string& uuid_string);

// Constructs a 128-bit UUID from a string representation in the following formats:
//
//   xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
//
// where x is one of the alphanumeric characters in the string 0123456789abcdefABCDEF.
//
// Returns false if the string does not represent a valid Bluetooth UUID. Otherwise returns true and
// populates |out_uuid|.
bool StringToUuid(const std::string& uuid_string, UUID* out_uuid);

// Equality operators
inline bool operator==(uint16_t lhs, const UUID& rhs) {
  return rhs == lhs;
}

inline bool operator==(uint32_t lhs, const UUID& rhs) {
  return rhs == lhs;
}

inline bool operator==(const UInt128& lhs, const UUID& rhs) {
  return rhs == lhs;
}

inline bool operator!=(uint16_t lhs, const UUID& rhs) {
  return rhs != lhs;
}

inline bool operator!=(uint32_t lhs, const UUID& rhs) {
  return rhs != lhs;
}

inline bool operator!=(const UInt128& lhs, const UUID& rhs) {
  return rhs != lhs;
}

}  // namespace common
}  // namespace bluetooth

// Specialization of std::hash for std::unordered_set, std::unordered_map, etc.
namespace std {

template<> struct hash<::bluetooth::common::UUID>
{
  size_t operator()(const ::bluetooth::common::UUID& k) const {
    return k.Hash();
  }
};

} // namespace std
