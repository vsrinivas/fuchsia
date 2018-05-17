// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include "gtest/gtest.h"

namespace btlib {
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

}  // namespace
}  // namespace util
}  // namespace sm
}  // namespace btlib
