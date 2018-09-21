// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hid/hid.h>
#include <hid/usages.h>

#include "garnet/lib/machina/hid_event_source.h"
#include "gtest/gtest.h"

// Yanked from system/ulib/hid/hid.c
#define KEYSET(bitmap, n) (bitmap[(n) >> 5] |= (1 << ((n)&31)))
#define KEYCLR(bitmap, n) (bitmap[(n) >> 5] &= ~(1 << ((n)&31)))

namespace machina {
namespace {

static constexpr size_t kInputQueueSize = 8;

static constexpr hid_keys_t kAllKeysUp = {};

// Utility class for performing verifications on the state of an
// InputDispatcher.
class InputDispatcherVerifier {
 public:
  InputDispatcherVerifier(InputDispatcherImpl* dispatcher) {
    Reset(dispatcher);
  }

  void Reset(InputDispatcherImpl* dispatcher) {
    queued_events_.clear();
    while (dispatcher->Keyboard()->size() > 0) {
      auto event = dispatcher->Keyboard()->Wait();
      queued_events_.push_back(std::move(event));
    }
  }

  size_t events() { return queued_events_.size(); }

  // Check for an evdev event between |min| and |max| inclusive in the
  // output stream.
  bool HasKeyEvent(size_t min, size_t max, uint32_t hid_usage,
                   fuchsia::ui::input::KeyboardEventPhase phase) {
    for (size_t i = min; i < max + 1 && i < queued_events_.size(); ++i) {
      auto& event = queued_events_[i];
      if (event.is_keyboard() && event.keyboard().hid_usage == hid_usage &&
          event.keyboard().phase == phase) {
        return true;
      }
    }
    return false;
  }

  bool HasKeyPress(size_t min, size_t max, uint32_t usage) {
    return HasKeyEvent(min, max, usage,
                       fuchsia::ui::input::KeyboardEventPhase::PRESSED);
  }

  bool HasKeyRelease(size_t min, size_t max, uint32_t usage) {
    return HasKeyEvent(min, max, usage,
                       fuchsia::ui::input::KeyboardEventPhase::RELEASED);
  }

 private:
  std::vector<fuchsia::ui::input::InputEvent> queued_events_;
};

TEST(HidEventSourceTest, HandleKeyPress) {
  InputDispatcherImpl dispatcher(kInputQueueSize);
  HidInputDevice hid_device(&dispatcher);

  // Set 'A' as pressed.
  hid_keys_t keys = {};
  KEYSET(keys.keymask, HID_USAGE_KEY_A);

  ASSERT_EQ(hid_device.HandleHidKeys(keys), ZX_OK);

  InputDispatcherVerifier verifier(&dispatcher);
  ASSERT_EQ(verifier.events(), 1u);
  EXPECT_TRUE(verifier.HasKeyPress(0, 0, HID_USAGE_KEY_A));
}

TEST(HidEventSourceTest, HandleMultipleKeyPress) {
  InputDispatcherImpl dispatcher(kInputQueueSize);
  HidInputDevice hid_device(&dispatcher);

  // Set 'ABCD' as pressed.
  hid_keys_t keys = {};
  KEYSET(keys.keymask, HID_USAGE_KEY_A);
  KEYSET(keys.keymask, HID_USAGE_KEY_B);
  KEYSET(keys.keymask, HID_USAGE_KEY_C);
  KEYSET(keys.keymask, HID_USAGE_KEY_D);

  ASSERT_EQ(hid_device.HandleHidKeys(keys), ZX_OK);

  InputDispatcherVerifier verifier(&dispatcher);
  ASSERT_EQ(verifier.events(), 4u);
  EXPECT_TRUE(verifier.HasKeyPress(0, 3, HID_USAGE_KEY_A));
  EXPECT_TRUE(verifier.HasKeyPress(0, 3, HID_USAGE_KEY_B));
  EXPECT_TRUE(verifier.HasKeyPress(0, 3, HID_USAGE_KEY_C));
  EXPECT_TRUE(verifier.HasKeyPress(0, 3, HID_USAGE_KEY_D));
}

TEST(HidEventSourceTest, HandleKeyRelease) {
  InputDispatcherImpl dispatcher(kInputQueueSize);
  HidInputDevice hid_device(&dispatcher);

  // Initialize with 'A' key pressed.
  hid_keys_t key_pressed_keys = {};
  KEYSET(key_pressed_keys.keymask, HID_USAGE_KEY_A);
  ASSERT_EQ(hid_device.HandleHidKeys(key_pressed_keys), ZX_OK);

  // Release all keys.
  InputDispatcherVerifier verifier(&dispatcher);
  ASSERT_EQ(hid_device.HandleHidKeys(kAllKeysUp), ZX_OK);
  verifier.Reset(&dispatcher);

  ASSERT_EQ(verifier.events(), 1u);
  EXPECT_TRUE(verifier.HasKeyRelease(0, 0, HID_USAGE_KEY_A));
}

TEST(HidEventSourceTest, HandleMultipleKeyRelease) {
  InputDispatcherImpl dispatcher(kInputQueueSize);
  HidInputDevice hid_device(&dispatcher);

  // Set 'ABCD' as pressed.
  hid_keys_t keys = {};
  KEYSET(keys.keymask, HID_USAGE_KEY_A);
  KEYSET(keys.keymask, HID_USAGE_KEY_B);
  KEYSET(keys.keymask, HID_USAGE_KEY_C);
  KEYSET(keys.keymask, HID_USAGE_KEY_D);
  ASSERT_EQ(hid_device.HandleHidKeys(keys), ZX_OK);

  // Release all keys.
  InputDispatcherVerifier verifier(&dispatcher);
  ASSERT_EQ(hid_device.HandleHidKeys(kAllKeysUp), ZX_OK);
  verifier.Reset(&dispatcher);

  ASSERT_EQ(verifier.events(), 4u);
  EXPECT_TRUE(verifier.HasKeyRelease(0, 3, HID_USAGE_KEY_A));
  EXPECT_TRUE(verifier.HasKeyRelease(0, 3, HID_USAGE_KEY_B));
  EXPECT_TRUE(verifier.HasKeyRelease(0, 3, HID_USAGE_KEY_C));
  EXPECT_TRUE(verifier.HasKeyRelease(0, 3, HID_USAGE_KEY_D));
}

// Test keys both being pressed and released in a single HID report.
TEST(HidEventSourceTest, HandleKeyPressAndRelease) {
  InputDispatcherImpl dispatcher(kInputQueueSize);
  HidInputDevice hid_device(&dispatcher);

  // Set 'AB' as pressed.
  hid_keys_t keys_ab = {};
  KEYSET(keys_ab.keymask, HID_USAGE_KEY_A);
  KEYSET(keys_ab.keymask, HID_USAGE_KEY_B);
  ASSERT_EQ(hid_device.HandleHidKeys(keys_ab), ZX_OK);

  // Release 'AB' and press 'CD'.
  hid_keys_t keys_cd = {};
  KEYSET(keys_cd.keymask, HID_USAGE_KEY_C);
  KEYSET(keys_cd.keymask, HID_USAGE_KEY_D);
  InputDispatcherVerifier verifier(&dispatcher);
  ASSERT_EQ(hid_device.HandleHidKeys(keys_cd), ZX_OK);
  verifier.Reset(&dispatcher);

  ASSERT_EQ(verifier.events(), 4u);
  EXPECT_TRUE(verifier.HasKeyPress(0, 3, HID_USAGE_KEY_C));
  EXPECT_TRUE(verifier.HasKeyPress(0, 3, HID_USAGE_KEY_D));
  EXPECT_TRUE(verifier.HasKeyRelease(0, 3, HID_USAGE_KEY_A));
  EXPECT_TRUE(verifier.HasKeyRelease(0, 3, HID_USAGE_KEY_B));
}

}  // namespace
}  // namespace machina
