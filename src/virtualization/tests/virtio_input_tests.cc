// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <string>

#include "guest_test.h"

namespace {

using ::fuchsia::ui::input::KeyboardEventPhase;
using ::testing::HasSubstr;

void PressAndReleaseKey(FakeScenic* scenic, KeyboardEventHidUsage usage) {
  scenic->BroadcastKeyEvent(usage, KeyboardEventPhase::PRESSED);
  scenic->BroadcastKeyEvent(usage, KeyboardEventPhase::RELEASED);
}

// Create an alias, as "TEST_F" requires the fixture name to be a valid C token.
using VirtioInputDebianGuestTest = GuestTest<DebianEnclosedGuest>;

TEST_F(VirtioInputDebianGuestTest, Input) {
  // Start the test.
  auto* guest_console = this->GetEnclosedGuest()->GetConsole();
  EXPECT_EQ(
      guest_console->SendBlocking(
          "/test_utils/virtio_input_test_util keyboard /dev/input/event*\n"),
      ZX_OK);

  // Wait for the test utility to print its string ("type ..."), and then
  // send keystrokes.
  EXPECT_EQ(guest_console->WaitForMarker("Type 'abc<shift>'"), ZX_OK);
  for (const auto key : {
           KeyboardEventHidUsage::KEY_A,
           KeyboardEventHidUsage::KEY_B,
           KeyboardEventHidUsage::KEY_C,
           KeyboardEventHidUsage::KEY_LSHIFT,
       }) {
    PressAndReleaseKey(this->GetEnclosedGuest()->GetScenic(), key);
  }

  // Ensure we passed.
  std::string result;
  EXPECT_EQ(guest_console->WaitForMarker("PASS", &result), ZX_OK);
}

}  // namespace
