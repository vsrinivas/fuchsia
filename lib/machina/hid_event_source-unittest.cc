// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/hid_event_source.h"

#include <fbl/vector.h>
#include <hid/hid.h>
#include <hid/usages.h>

#include "gtest/gtest.h"

// Yanked from system/ulib/hid/hid.c
#define KEYSET(bitmap, n) (bitmap[(n) >> 5] |= (1 << ((n)&31)))
#define KEYCLR(bitmap, n) (bitmap[(n) >> 5] &= ~(1 << ((n)&31)))

namespace machina {
namespace {

static constexpr size_t kInputQueueSize = 8;

static const hid_keys_t kAllKeysUp = {};

// Utility class for performing verifications on the state of an
// InputDispatcher.
class InputDispatcherVerifier {
 public:
  InputDispatcherVerifier(InputDispatcher* dispatcher) { Reset(dispatcher); }

  void Reset(InputDispatcher* dispatcher) {
    queued_events_.reset();
    while (dispatcher->size() > 0) {
      InputEvent event = dispatcher->Wait();
      queued_events_.push_back(event);
    }
  }

  size_t events() { return queued_events_.size(); }

  // Check for an evdev event between |min| and |max| inclusive in the
  // output stream.
  bool HasKeyEvent(size_t min, size_t max, uint32_t hid_usage, KeyState state) {
    for (size_t i = min; i < max + 1 && i < queued_events_.size(); ++i) {
      auto event = queued_events_[i];
      if (event.type == InputEventType::KEYBOARD &&
          event.key.hid_usage == hid_usage && event.key.state == state) {
        return true;
      }
    }
    return false;
  }

  bool HasKeyPress(size_t min, size_t max, uint32_t usage) {
    return HasKeyEvent(min, max, usage, KeyState::PRESSED);
  }

  bool HasKeyRelease(size_t min, size_t max, uint32_t usage) {
    return HasKeyEvent(min, max, usage, KeyState::RELEASED);
  }

  bool HasBarrier(size_t i) {
    auto event = queued_events_[i];
    return event.type == InputEventType::BARRIER;
  }

 private:
  fbl::Vector<InputEvent> queued_events_;
};

TEST(HidEventSourceTest, HandleKeyPress) {
  InputDispatcher dispatcher(kInputQueueSize);
  HidInputDevice hid_device(&dispatcher);

  // Set 'A' as pressed.
  hid_keys_t keys = {};
  KEYSET(keys.keymask, HID_USAGE_KEY_A);

  ASSERT_EQ(hid_device.HandleHidKeys(keys), ZX_OK);

  InputDispatcherVerifier verifier(&dispatcher);
  ASSERT_EQ(verifier.events(), 2u);
  EXPECT_TRUE(verifier.HasKeyPress(0, 0, HID_USAGE_KEY_A));
  EXPECT_TRUE(verifier.HasBarrier(1));
}

TEST(HidEventSourceTest, HandleMultipleKeyPress) {
  InputDispatcher dispatcher(kInputQueueSize);
  HidInputDevice hid_device(&dispatcher);

  // Set 'ABCD' as pressed.
  hid_keys_t keys = {};
  KEYSET(keys.keymask, HID_USAGE_KEY_A);
  KEYSET(keys.keymask, HID_USAGE_KEY_B);
  KEYSET(keys.keymask, HID_USAGE_KEY_C);
  KEYSET(keys.keymask, HID_USAGE_KEY_D);

  ASSERT_EQ(hid_device.HandleHidKeys(keys), ZX_OK);

  InputDispatcherVerifier verifier(&dispatcher);
  ASSERT_EQ(verifier.events(), 5u);
  EXPECT_TRUE(verifier.HasKeyPress(0, 3, HID_USAGE_KEY_A));
  EXPECT_TRUE(verifier.HasKeyPress(0, 3, HID_USAGE_KEY_B));
  EXPECT_TRUE(verifier.HasKeyPress(0, 3, HID_USAGE_KEY_C));
  EXPECT_TRUE(verifier.HasKeyPress(0, 3, HID_USAGE_KEY_D));
  EXPECT_TRUE(verifier.HasBarrier(4));
}

TEST(HidEventSourceTest, HandleKeyRelease) {
  InputDispatcher dispatcher(kInputQueueSize);
  HidInputDevice hid_device(&dispatcher);

  // Initialize with 'A' key pressed.
  hid_keys_t key_pressed_keys = {};
  KEYSET(key_pressed_keys.keymask, HID_USAGE_KEY_A);
  ASSERT_EQ(hid_device.HandleHidKeys(key_pressed_keys), ZX_OK);

  // Release all keys.
  InputDispatcherVerifier verifier(&dispatcher);
  ASSERT_EQ(hid_device.HandleHidKeys(kAllKeysUp), ZX_OK);
  verifier.Reset(&dispatcher);

  ASSERT_EQ(verifier.events(), 2u);
  EXPECT_TRUE(verifier.HasKeyRelease(0, 0, HID_USAGE_KEY_A));
  EXPECT_TRUE(verifier.HasBarrier(1));
}

TEST(HidEventSourceTest, HandleMultipleKeyRelease) {
  InputDispatcher dispatcher(kInputQueueSize);
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

  ASSERT_EQ(verifier.events(), 5u);
  EXPECT_TRUE(verifier.HasKeyRelease(0, 3, HID_USAGE_KEY_A));
  EXPECT_TRUE(verifier.HasKeyRelease(0, 3, HID_USAGE_KEY_B));
  EXPECT_TRUE(verifier.HasKeyRelease(0, 3, HID_USAGE_KEY_C));
  EXPECT_TRUE(verifier.HasKeyRelease(0, 3, HID_USAGE_KEY_D));
  EXPECT_TRUE(verifier.HasBarrier(4));
}

// Test keys both being pressed and released in a single HID report.
TEST(HidEventSourceTest, HandleKeyPressAndRelease) {
  InputDispatcher dispatcher(kInputQueueSize);
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

  ASSERT_EQ(verifier.events(), 5u);
  EXPECT_TRUE(verifier.HasKeyPress(0, 3, HID_USAGE_KEY_C));
  EXPECT_TRUE(verifier.HasKeyPress(0, 3, HID_USAGE_KEY_D));
  EXPECT_TRUE(verifier.HasKeyRelease(0, 3, HID_USAGE_KEY_A));
  EXPECT_TRUE(verifier.HasKeyRelease(0, 3, HID_USAGE_KEY_B));
  EXPECT_TRUE(verifier.HasBarrier(4));
}

}  // namespace
}  // namespace machina
