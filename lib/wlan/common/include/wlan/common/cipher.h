// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace wlan {
namespace common {
namespace cipher {

// IEEE Std 802.11-2016, 9.4.2.25.2, Table 9-131

const uint8_t kStandardOui[3] = {0x00, 0x0F, 0xAC};
const uint8_t kUseGroupCipherSuite = 0;
const uint8_t kWep40 = 1;
const uint8_t kTkip = 2;
// Cipher Suite Type 3 is reserved
const uint8_t kCcmp128 = 4;
const uint8_t kWep104 = 5;
const uint8_t kBipCmac128 = 6;
const uint8_t kGroupAddressedTrafficNotAllowed = 7;
const uint8_t kGcmp128 = 8;
const uint8_t kGcmp256 = 9;
const uint8_t kCcmp256 = 10;
const uint8_t kBipGmac128 = 11;
const uint8_t kBipGmac256 = 12;
const uint8_t kBipCmac256 = 13;

// IEEE Std 802.11-2016, 12.7.2, Table 12-4

const uint8_t kWep40KeyLenBytes = 5;
const uint8_t kWep104KeyLenBytes = 13;
const uint8_t kTkipKeyLenBytes = 32;
const uint8_t kCcmp128KeyLenBytes = 16;
const uint8_t kBipCmac128KeyLenBytes = 16;
const uint8_t kGcmp128KeyLenBytes = 16;
const uint8_t kGcmp256KeyLenBytes = 32;
const uint8_t kCcmp256KeyLenBytes = 32;
const uint8_t kBipGmac128KeyLenBytes = 16;
const uint8_t kBipGmac256KeyLenBytes = 32;
const uint8_t kBipCmac256LenBytes = 32;

}  // namespace cipher
}  // namespace common
}  // namespace wlan
