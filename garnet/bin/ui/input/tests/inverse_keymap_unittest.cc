// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/input/inverse_keymap.h"

#include <hid/hid.h>
#include <hid/usages.h>

#include "gtest/gtest.h"

namespace input {

namespace {

// Initializes an inverse QWERTY keymap.
class InverseKeymapTest : public testing::Test {
 protected:
  InverseKeymapTest() : keymap_(InvertKeymap(qwerty_map)) {}

  const InverseKeymap keymap_;
};

// A version of |Keystroke| with a more concise, binary shift state.
struct SimpleKeystroke {
  uint32_t usage;
  bool shift;
};

// Runs checks on a key sequence, using |SimpleKeystroke|s as a convenience
// representation. Null usages on the expectation indicate that no (non-shift)
// key is expected to be pressed. (Shift is governed separately.)
void CheckKeySequence(const KeySequence& actual, const std::vector<SimpleKeystroke>& expected) {
  ASSERT_EQ(actual.size(), expected.size());

  for (size_t i = 0; i < actual.size(); ++i) {
    const std::vector<uint32_t>& pressed_keys = actual[i]->pressed_keys;

    uint32_t key = 0;
    bool shift = false;
    for (uint32_t usage : pressed_keys) {
      EXPECT_NE(usage, 0u);

      if (usage == HID_USAGE_KEY_LEFT_SHIFT) {
        EXPECT_FALSE(shift) << "Duplicate shift key in report";
        shift = true;
      } else {
        EXPECT_EQ(key, 0u) << "Multiple normal keys in report";
        key = usage;
      }
    }

    EXPECT_EQ(key, expected[i].usage);
    EXPECT_EQ(shift, expected[i].shift);
  }
}

}  // namespace

TEST_F(InverseKeymapTest, PlainKey) {
  auto it = keymap_.find('a');
  ASSERT_NE(it, keymap_.end());
  const Keystroke& keystroke = it->second;
  EXPECT_EQ(keystroke.usage, HID_USAGE_KEY_A);
  EXPECT_EQ(keystroke.shift, Keystroke::Shift::kNo);
}

TEST_F(InverseKeymapTest, ShiftKey) {
  auto it = keymap_.find('A');
  ASSERT_NE(it, keymap_.end());
  const Keystroke& keystroke = it->second;
  EXPECT_EQ(keystroke.usage, HID_USAGE_KEY_A);
  EXPECT_EQ(keystroke.shift, Keystroke::Shift::kYes);
}

// The primary facility under test in the following cases is
// |DeriveKeySequence|. See the inverse_keymap.h/cc for details on expected
// behavior.

TEST_F(InverseKeymapTest, Lowercase) {
  KeySequence key_sequence;
  bool ok;
  std::tie(key_sequence, ok) = DeriveKeySequence(keymap_, "lowercase");
  ASSERT_TRUE(ok);

  CheckKeySequence(key_sequence, {{HID_USAGE_KEY_L, false},
                                  {HID_USAGE_KEY_O, false},
                                  {HID_USAGE_KEY_W, false},
                                  {HID_USAGE_KEY_E, false},
                                  {HID_USAGE_KEY_R, false},
                                  {HID_USAGE_KEY_C, false},
                                  {HID_USAGE_KEY_A, false},
                                  {HID_USAGE_KEY_S, false},
                                  {HID_USAGE_KEY_E, false},
                                  {}});
}

TEST_F(InverseKeymapTest, Sentence) {
  KeySequence key_sequence;
  bool ok;
  std::tie(key_sequence, ok) = DeriveKeySequence(keymap_, "Hello, world!");
  ASSERT_TRUE(ok);

  CheckKeySequence(key_sequence, {{0, true},
                                  {HID_USAGE_KEY_H, true},
                                  {},
                                  {HID_USAGE_KEY_E, false},
                                  {HID_USAGE_KEY_L, false},
                                  {},
                                  {HID_USAGE_KEY_L, false},
                                  {HID_USAGE_KEY_O, false},
                                  {HID_USAGE_KEY_COMMA, false},
                                  {HID_USAGE_KEY_SPACE, false},
                                  {HID_USAGE_KEY_W, false},
                                  {HID_USAGE_KEY_O, false},
                                  {HID_USAGE_KEY_R, false},
                                  {HID_USAGE_KEY_L, false},
                                  {HID_USAGE_KEY_D, false},
                                  {0, true},
                                  {HID_USAGE_KEY_1, true},
                                  {}});
}

TEST_F(InverseKeymapTest, HoldShift) {
  KeySequence key_sequence;
  bool ok;
  std::tie(key_sequence, ok) = DeriveKeySequence(keymap_, "ALL'S WELL!");
  ASSERT_TRUE(ok);

  CheckKeySequence(key_sequence, {{0, true},
                                  {HID_USAGE_KEY_A, true},
                                  {HID_USAGE_KEY_L, true},
                                  {0, true},
                                  {HID_USAGE_KEY_L, true},
                                  {},
                                  {HID_USAGE_KEY_APOSTROPHE, false},
                                  {0, true},
                                  {HID_USAGE_KEY_S, true},
                                  {HID_USAGE_KEY_SPACE, true},
                                  {HID_USAGE_KEY_W, true},
                                  {HID_USAGE_KEY_E, true},
                                  {HID_USAGE_KEY_L, true},
                                  {0, true},
                                  {HID_USAGE_KEY_L, true},
                                  {HID_USAGE_KEY_1, true},
                                  {}});
}

}  // namespace input
