// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include "gtest/gtest.h"

#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"

namespace btlib {

using common::ContainersEqual;
using common::CreateStaticByteBuffer;
using common::DeviceAddress;
using common::UInt128;

namespace sm {
namespace util {
namespace {

TEST(SMP_UtilTest, SelectPairingMethodOOB) {
  // In SC OOB is selected if either device has OOB data.
  EXPECT_EQ(PairingMethod::kOutOfBand,
            SelectPairingMethod(true /* sc */, true /* local_oob */,
                                false /* peer_oob */, true /* mitm */,
                                IOCapability::kKeyboardDisplay /* local */,
                                IOCapability::kKeyboardDisplay /* peer */));
  EXPECT_EQ(PairingMethod::kOutOfBand,
            SelectPairingMethod(true /* sc */, false /* local_oob */,
                                true /* peer_oob */, true /* mitm */,
                                IOCapability::kKeyboardDisplay /* local */,
                                IOCapability::kKeyboardDisplay /* peer */));
  EXPECT_NE(PairingMethod::kOutOfBand,
            SelectPairingMethod(true /* sc */, false /* local_oob */,
                                false /* peer_oob */, true /* mitm */,
                                IOCapability::kKeyboardDisplay /* local */,
                                IOCapability::kKeyboardDisplay /* peer */));

  // In legacy OOB is selected if both devices have OOB data.
  EXPECT_EQ(PairingMethod::kOutOfBand,
            SelectPairingMethod(false /* sc */, true /* local_oob */,
                                true /* peer_oob */, true /* mitm */,
                                IOCapability::kKeyboardDisplay /* local */,
                                IOCapability::kKeyboardDisplay /* peer */));
  EXPECT_NE(PairingMethod::kOutOfBand,
            SelectPairingMethod(false /* sc */, false /* local_oob */,
                                true /* peer_oob */, true /* mitm */,
                                IOCapability::kKeyboardDisplay /* local */,
                                IOCapability::kKeyboardDisplay /* peer */));
  EXPECT_NE(PairingMethod::kOutOfBand,
            SelectPairingMethod(false /* sc */, true /* local_oob */,
                                false /* peer_oob */, true /* mitm */,
                                IOCapability::kKeyboardDisplay /* local */,
                                IOCapability::kKeyboardDisplay /* peer */));
  EXPECT_NE(PairingMethod::kOutOfBand,
            SelectPairingMethod(false /* sc */, false /* local_oob */,
                                false /* peer_oob */, true /* mitm */,
                                IOCapability::kKeyboardDisplay /* local */,
                                IOCapability::kKeyboardDisplay /* peer */));
}

TEST(SMP_UtilTest, SelectPairingMethodNoMITM) {
  // The pairing method should be "Just Works" if neither device requires MITM
  // protection, regardless of other parameters.
  EXPECT_EQ(PairingMethod::kJustWorks,
            SelectPairingMethod(true /* sc */, false /* local_oob */,
                                false /* peer_oob */, false /* mitm */,
                                IOCapability::kKeyboardDisplay /* local */,
                                IOCapability::kKeyboardDisplay /* peer */));

  // Shouldn't default to "Just Works" if at least one device requires MITM
  // protection.
  EXPECT_NE(PairingMethod::kJustWorks,
            SelectPairingMethod(false /* sc */, false /* local_oob */,
                                false /* peer_oob */, true /* mitm */,
                                IOCapability::kKeyboardDisplay /* local */,
                                IOCapability::kKeyboardDisplay /* peer */));
}

// Tests all combinations that result in the "Just Works" pairing method.
TEST(SMP_UtilTest, SelectPairingMethodJustWorks) {
  // Local: DisplayOnly
  EXPECT_EQ(PairingMethod::kJustWorks,
            SelectPairingMethod(true /* sc */, false /* local_oob */,
                                false /* peer_oob */, true /* mitm */,
                                IOCapability::kDisplayOnly /* local */,
                                IOCapability::kDisplayOnly /* peer */));
  EXPECT_EQ(PairingMethod::kJustWorks,
            SelectPairingMethod(true /* sc */, false /* local_oob */,
                                false /* peer_oob */, true /* mitm */,
                                IOCapability::kDisplayOnly /* local */,
                                IOCapability::kDisplayYesNo /* peer */));
  EXPECT_EQ(PairingMethod::kJustWorks,
            SelectPairingMethod(true /* sc */, false /* local_oob */,
                                false /* peer_oob */, true /* mitm */,
                                IOCapability::kDisplayOnly /* local */,
                                IOCapability::kNoInputNoOutput /* peer */));

  // Local: DisplayYesNo
  EXPECT_EQ(PairingMethod::kJustWorks,
            SelectPairingMethod(true /* sc */, false /* local_oob */,
                                false /* peer_oob */, true /* mitm */,
                                IOCapability::kDisplayYesNo /* local */,
                                IOCapability::kDisplayOnly /* peer */));
  // If both devices are DisplayYesNo, then "Just Works" is selected for LE
  // legacy pairing (i.e. at least one device doesn't support Secure
  // Connections).
  EXPECT_EQ(PairingMethod::kJustWorks,
            SelectPairingMethod(false /* sc */, false /* local_oob */,
                                false /* peer_oob */, true /* mitm */,
                                IOCapability::kDisplayYesNo /* local */,
                                IOCapability::kDisplayYesNo /* peer */));
  EXPECT_EQ(PairingMethod::kJustWorks,
            SelectPairingMethod(true /* sc */, false /* local_oob */,
                                false /* peer_oob */, true /* mitm */,
                                IOCapability::kDisplayYesNo /* local */,
                                IOCapability::kNoInputNoOutput /* peer */));

  // Local: KeyboardOnly
  EXPECT_EQ(PairingMethod::kJustWorks,
            SelectPairingMethod(true /* sc */, false /* local_oob */,
                                false /* peer_oob */, true /* mitm */,
                                IOCapability::kKeyboardOnly /* local */,
                                IOCapability::kNoInputNoOutput /* peer */));

  // Local: NoInputNoOutput. Always "Just Works".
  EXPECT_EQ(PairingMethod::kJustWorks,
            SelectPairingMethod(true /* sc */, false /* local_oob */,
                                false /* peer_oob */, true /* mitm */,
                                IOCapability::kNoInputNoOutput /* local */,
                                IOCapability::kDisplayOnly /* peer */));
  EXPECT_EQ(PairingMethod::kJustWorks,
            SelectPairingMethod(true /* sc */, false /* local_oob */,
                                false /* peer_oob */, true /* mitm */,
                                IOCapability::kNoInputNoOutput /* local */,
                                IOCapability::kDisplayYesNo /* peer */));
  EXPECT_EQ(PairingMethod::kJustWorks,
            SelectPairingMethod(true /* sc */, false /* local_oob */,
                                false /* peer_oob */, true /* mitm */,
                                IOCapability::kNoInputNoOutput /* local */,
                                IOCapability::kKeyboardOnly /* peer */));
  EXPECT_EQ(PairingMethod::kJustWorks,
            SelectPairingMethod(true /* sc */, false /* local_oob */,
                                false /* peer_oob */, true /* mitm */,
                                IOCapability::kNoInputNoOutput /* local */,
                                IOCapability::kNoInputNoOutput /* peer */));
  EXPECT_EQ(PairingMethod::kJustWorks,
            SelectPairingMethod(true /* sc */, false /* local_oob */,
                                false /* peer_oob */, true /* mitm */,
                                IOCapability::kNoInputNoOutput /* local */,
                                IOCapability::kKeyboardDisplay /* peer */));

  // Local: KeyboardDisplay
  EXPECT_EQ(PairingMethod::kJustWorks,
            SelectPairingMethod(true /* sc */, false /* local_oob */,
                                false /* peer_oob */, true /* mitm */,
                                IOCapability::kKeyboardDisplay /* local */,
                                IOCapability::kNoInputNoOutput /* peer */));
}

// Tests all combinations that result in the "Passkey Entry" pairing method.
TEST(SMP_UtilTest, SelectPairingMethodPasskeyEntry) {
  // Local: DisplayOnly
  EXPECT_EQ(PairingMethod::kPasskeyEntry,
            SelectPairingMethod(true /* sc */, false /* local_oob */,
                                false /* peer_oob */, true /* mitm */,
                                IOCapability::kDisplayOnly /* local */,
                                IOCapability::kKeyboardOnly /* peer */));
  EXPECT_EQ(PairingMethod::kPasskeyEntry,
            SelectPairingMethod(true /* sc */, false /* local_oob */,
                                false /* peer_oob */, true /* mitm */,
                                IOCapability::kDisplayOnly /* local */,
                                IOCapability::kKeyboardDisplay /* peer */));

  // Local: DisplayYesNo
  EXPECT_EQ(PairingMethod::kPasskeyEntry,
            SelectPairingMethod(true /* sc */, false /* local_oob */,
                                false /* peer_oob */, true /* mitm */,
                                IOCapability::kDisplayYesNo /* local */,
                                IOCapability::kKeyboardOnly /* peer */));
  // If the peer has a display then use "Passkey Entry" only for LE Legacy
  // pairing.
  EXPECT_EQ(PairingMethod::kPasskeyEntry,
            SelectPairingMethod(false /* sc */, false /* local_oob */,
                                false /* peer_oob */, true /* mitm */,
                                IOCapability::kDisplayYesNo /* local */,
                                IOCapability::kKeyboardDisplay /* peer */));

  // Local: KeyboardOnly
  EXPECT_EQ(PairingMethod::kPasskeyEntry,
            SelectPairingMethod(true /* sc */, false /* local_oob */,
                                false /* peer_oob */, true /* mitm */,
                                IOCapability::kKeyboardOnly /* local */,
                                IOCapability::kDisplayOnly /* peer */));
  EXPECT_EQ(PairingMethod::kPasskeyEntry,
            SelectPairingMethod(true /* sc */, false /* local_oob */,
                                false /* peer_oob */, true /* mitm */,
                                IOCapability::kKeyboardOnly /* local */,
                                IOCapability::kDisplayYesNo /* peer */));
  EXPECT_EQ(PairingMethod::kPasskeyEntry,
            SelectPairingMethod(true /* sc */, false /* local_oob */,
                                false /* peer_oob */, true /* mitm */,
                                IOCapability::kKeyboardOnly /* local */,
                                IOCapability::kKeyboardOnly /* peer */));
  EXPECT_EQ(PairingMethod::kPasskeyEntry,
            SelectPairingMethod(true /* sc */, false /* local_oob */,
                                false /* peer_oob */, true /* mitm */,
                                IOCapability::kKeyboardOnly /* local */,
                                IOCapability::kKeyboardDisplay /* peer */));
}

// Tests all combinations that result in the "Numeric Comparison" pairing
// method. This will be selected in certain I/O capability combinations only if
// both devices support Secure Connections.
TEST(SMP_UtilTest, SelectPairingMethodNumericComparison) {
  // Local: DisplayYesNo
  EXPECT_EQ(PairingMethod::kNumericComparison,
            SelectPairingMethod(true /* sc */, false /* local_oob */,
                                false /* peer_oob */, true /* mitm */,
                                IOCapability::kDisplayYesNo /* local */,
                                IOCapability::kDisplayYesNo /* peer */));
  EXPECT_EQ(PairingMethod::kNumericComparison,
            SelectPairingMethod(true /* sc */, false /* local_oob */,
                                false /* peer_oob */, true /* mitm */,
                                IOCapability::kDisplayYesNo /* local */,
                                IOCapability::kKeyboardDisplay /* peer */));

  // Local: KeyboardDisplay
  EXPECT_EQ(PairingMethod::kNumericComparison,
            SelectPairingMethod(true /* sc */, false /* local_oob */,
                                false /* peer_oob */, true /* mitm */,
                                IOCapability::kKeyboardDisplay /* local */,
                                IOCapability::kDisplayYesNo /* peer */));
  EXPECT_EQ(PairingMethod::kNumericComparison,
            SelectPairingMethod(true /* sc */, false /* local_oob */,
                                false /* peer_oob */, true /* mitm */,
                                IOCapability::kKeyboardDisplay /* local */,
                                IOCapability::kKeyboardDisplay /* peer */));
}

// Tests "c1" using the sample data from Vol 3, Part H, 2.2.3.
TEST(SMP_UtilTest, C1) {
  const UInt128 tk{{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
  const UInt128 r{{0xE0, 0x2E, 0x70, 0xC6, 0x4E, 0x27, 0x88, 0x63, 0x0E, 0x6F,
                   0xAD, 0x56, 0x21, 0xD5, 0x83, 0x57}};
  const auto preq =
      CreateStaticByteBuffer(0x01, 0x01, 0x00, 0x00, 0x10, 0x07, 0x07);
  const auto pres =
      CreateStaticByteBuffer(0x02, 0x03, 0x00, 0x00, 0x08, 0x00, 0x05);
  const DeviceAddress initiator_addr(DeviceAddress::Type::kLERandom,
                                     "A1:A2:A3:A4:A5:A6");
  const DeviceAddress responder_addr(DeviceAddress::Type::kLEPublic,
                                     "B1:B2:B3:B4:B5:B6");

  const UInt128 kExpected{{0x86, 0x3B, 0xF1, 0xBE, 0xC5, 0x4D, 0xA7, 0xD2, 0xEA,
                           0x88, 0x89, 0x87, 0xEF, 0x3F, 0x1E, 0x1E}};

  UInt128 result;
  C1(tk, r, preq, pres, initiator_addr, responder_addr, &result);
  EXPECT_TRUE(ContainersEqual(kExpected, result));
}

// Tests "s1" using the sample data from Vol 3, Part H, 2.2.4.
TEST(SMP_UtilTest, S1) {
  const UInt128 tk{{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
  const UInt128 r1{{0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x09, 0xA0,
                    0xB0, 0xC0, 0xD0, 0xE0, 0xF0, 0x00}};
  const UInt128 r2{{0x00, 0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x08, 0x07,
                    0x06, 0x05, 0x04, 0x03, 0x02, 0x01}};

  const UInt128 kExpected{{0x62, 0xA0, 0x6D, 0x79, 0xAE, 0x16, 0x42, 0x5B, 0x9B,
                           0xF4, 0xB0, 0xE8, 0xF0, 0xE1, 0x1F, 0x9A}};

  UInt128 result;
  S1(tk, r1, r2, &result);
  EXPECT_TRUE(ContainersEqual(kExpected, result));
}

}  // namespace
}  // namespace util
}  // namespace sm
}  // namespace btlib
