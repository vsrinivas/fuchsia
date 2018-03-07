// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/drivers/bluetooth/lib/common/uuid.h"

namespace btlib {
namespace gatt {

// 16-bit Attribute Types defined by the GATT profile (Vol 3, Part G, 3.4).
namespace types {

constexpr uint16_t kPrimaryService16 = 0x2800;
constexpr uint16_t kSecondaryService16 = 0x2801;
constexpr uint16_t kIncludeDeclaration16 = 0x2802;
constexpr uint16_t kCharacteristicDeclaration16 = 0x2803;
constexpr uint16_t kCharacteristicExtProperties16 = 0x2900;
constexpr uint16_t kCharacteristicUserDescription16 = 0x2901;
constexpr uint16_t kClientCharacteristicConfig16 = 0x2902;
constexpr uint16_t kServerCharacteristicConfig16 = 0x2903;
constexpr uint16_t kCharacteristicFormat16 = 0x2904;
constexpr uint16_t kCharacteristicAggregateFormat16 = 0x2905;

constexpr common::UUID kPrimaryService(kPrimaryService16);
constexpr common::UUID kSecondaryService(kSecondaryService16);
constexpr common::UUID kIncludeDeclaration(kIncludeDeclaration16);
constexpr common::UUID kCharacteristicDeclaration(kCharacteristicDeclaration16);
constexpr common::UUID kCharacteristicExtProperties(
    kCharacteristicExtProperties16);
constexpr common::UUID kCharacteristicUserDescription(
    kCharacteristicUserDescription16);
constexpr common::UUID kClientCharacteristicConfig(
    kClientCharacteristicConfig16);
constexpr common::UUID kServerCharacteristicConfig(
    kServerCharacteristicConfig16);
constexpr common::UUID kCharacteristicFormat(kCharacteristicFormat16);
constexpr common::UUID kCharacteristicAggregateFormat(
    kCharacteristicAggregateFormat16);

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
