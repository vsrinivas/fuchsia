// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "uuid.h"

#include <endian.h>
#include <cinttypes>

#include "apps/bluetooth/lib/common/byte_buffer.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_number_conversions.h"
#include "lib/ftl/strings/string_printf.h"

namespace bluetooth {
namespace common {
namespace {

// The Bluetooth Base UUID defines the first value in the range UUIDs reserved by the Bluetooth SIG
// for often-used and officially registered UUIDs. This UUID is defined as
//
//    "00000000-0000-1000-8000-00805F9B34FB"
//
// (see Core Spec v5.0, Vol 3, Part B, Section 2.5.1)
constexpr UInt128 kBaseUUID = {{0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00}};

// Format string that can be passed to sscanf. This allows sscanf to convert each octet into a
// uint8_t.
constexpr char kScanUUIDFormatString[] =
    "%2" SCNx8 "%2" SCNx8 "%2" SCNx8 "%2" SCNx8
    "-"
    "%2" SCNx8 "%2" SCNx8
    "-"
    "%2" SCNx8 "%2" SCNx8
    "-"
    "%2" SCNx8 "%2" SCNx8
    "-"
    "%2" SCNx8 "%2" SCNx8 "%2" SCNx8 "%2" SCNx8 "%2" SCNx8 "%2" SCNx8;

// A 16-bit or 32-bit UUID can be converted to a 128-bit UUID using the following formula:
//
//   16-/32-bit value * 2^96 + Bluetooth_Base_UUID
//
// This is the equivalent of modifying the higher order bytes of the base UUID starting at octet 12
// (96 bits = 12 bytes).
//
// (see Core Spec v5.0, Vol 3, Part B, Section 2.5.1)
constexpr size_t kBaseOffset = 12;

}  // namespace

UUID::UUID(const UInt128& uuid128) : type_(Type::k128Bit), value_(uuid128) {}

UUID::UUID(uint16_t uuid16) : type_(Type::k16Bit), value_(kBaseUUID) {
  uint16_t* bytes = reinterpret_cast<uint16_t*>(value_.data() + kBaseOffset);
  *bytes = htole16(uuid16);
}

UUID::UUID(uint32_t uuid32)
    // If the value of |uuid32| looks like 0x0000xxxx, then store this as a 16-bit UUID.
    : type_(uuid32 > std::numeric_limits<uint16_t>::max() ? Type::k32Bit : Type::k16Bit),
      value_(kBaseUUID) {
  uint32_t* bytes = reinterpret_cast<uint32_t*>(value_.data() + kBaseOffset);
  *bytes = htole32(uuid32);
}

UUID::UUID() : type_(Type::k128Bit) {
  value_.fill(0);
}

bool UUID::operator==(const UUID& uuid) const {
  return value_ == uuid.value_;
}

bool UUID::operator==(uint16_t uuid16) const {
  if (type_ == Type::k16Bit) return uuid16 == ValueAs16Bit();

  // Quick conversion is not possible; compare as two 128-bit UUIDs.
  return *this == UUID(uuid16);
}

bool UUID::operator==(uint32_t uuid32) const {
  if (type_ != Type::k128Bit) return uuid32 == ValueAs32Bit();

  // Quick conversion is not possible; compare as two 128-bit UUIDs.
  return *this == UUID(uuid32);
}

bool UUID::operator==(const UInt128& uuid128) const {
  return value_ == uuid128;
}

bool UUID::CompareBytes(const common::ByteBuffer& bytes) const {
  if (bytes.GetSize() == 2) {
    return (*this == le16toh(*reinterpret_cast<const uint16_t*>(bytes.GetData())));
  }
  if (bytes.GetSize() == 4) {
    return (*this == le32toh(*reinterpret_cast<const uint32_t*>(bytes.GetData())));
  }
  if (bytes.GetSize() == 16) {
    return (*this == *reinterpret_cast<const UInt128*>(bytes.GetData()));
  }

  return false;
}

std::string UUID::ToString() const {
  return ftl::StringPrintf("%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                           value_[15], value_[14], value_[13], value_[12], value_[11], value_[10],
                           value_[9], value_[8], value_[7], value_[6], value_[5], value_[4],
                           value_[3], value_[2], value_[1], value_[0]);
}

uint16_t UUID::ValueAs16Bit() const {
  FTL_DCHECK(type_ == Type::k16Bit);

  return le16toh(*reinterpret_cast<const uint16_t*>(value_.data() + kBaseOffset));
}

uint32_t UUID::ValueAs32Bit() const {
  FTL_DCHECK(type_ != Type::k128Bit);

  return le32toh(*reinterpret_cast<const uint32_t*>(value_.data() + kBaseOffset));
}

bool StringToUUID(const std::string& uuid_string, UUID* out_uuid) {
  FTL_DCHECK(out_uuid);

  // This is a 36 character string, including 4 "-" characters and two characters for each of the
  // 16-octets that form the 128-bit UUID.
  if (uuid_string.length() != 36) return false;

  UInt128 value;
  int result =
      std::sscanf(uuid_string.c_str(), kScanUUIDFormatString, &value[15], &value[14], &value[13],
                  &value[12], &value[11], &value[10], &value[9], &value[8], &value[7], &value[6],
                  &value[5], &value[4], &value[3], &value[2], &value[1], &value[0]);

  if (result != value.size()) return false;

  *out_uuid = UUID(value);
  return true;
}

}  // namespace common
}  // namespace bluetooth
