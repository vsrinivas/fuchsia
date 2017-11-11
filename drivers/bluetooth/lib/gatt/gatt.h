// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/drivers/bluetooth/lib/common/uuid.h"

namespace btlib {
namespace gatt {

// 16-bit Attribute Types defined by the GATT profile (Vol 3, Part G, 3.4).
namespace types {

constexpr ::btlib::common::UUID kPrimaryService((uint16_t)0x2800);
constexpr ::btlib::common::UUID kSecondaryService((uint16_t)0x2801);
constexpr ::btlib::common::UUID kIncludeDeclaration((uint16_t)0x2802);
constexpr ::btlib::common::UUID kCharacteristicDeclaration((uint16_t)0x2803);
constexpr ::btlib::common::UUID kCharacteristicExtProperties((uint16_t)0x2900);
constexpr ::btlib::common::UUID kCharacteristicUserDescription(
    (uint16_t)0x2901);
constexpr ::btlib::common::UUID kClientCharacteristicConfig((uint16_t)0x2902);
constexpr ::btlib::common::UUID kServerCharacteristicConfig((uint16_t)0x2903);
constexpr ::btlib::common::UUID kCharacteristicFormat((uint16_t)0x2904);
constexpr ::btlib::common::UUID kCharacteristicAggregateFormat(
    (uint16_t)0x2905);

}  // namespace types

// Possible values that can be used in a "Characteristic Properties" bitfield.
// (see Vol 3, Part G, 3.3.1.1)
enum Property : uint8_t {
  kBroadcast = 0x01,
  kRead = 0x02,
  kWriteWithoutResponse = 0x04,
  kWrite = 0x08,
  kNotify = 0x10,
  kIndicate = 0x20,
  kAuthenticatedSignedWrites = 0x40,
  kExtendedProperties = 0x80,
};

// Values for "Characteristic Extended Properties" bitfield.
// (see Vol 3, Part G, 3.3.3.1)
enum ExtendedProperty : uint16_t {
  kReliableWrite = 0x0001,
  kWritableAuxiliaries = 0x0002,
};

}  // namespace gatt
}  // namespace btlib
