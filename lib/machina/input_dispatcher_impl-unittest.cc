// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/input_dispatcher_impl.h"
#include "gtest/gtest.h"

namespace machina {
namespace {

static fuchsia::ui::input::InputEvent MakeKeyboardPressedEvent(
    uint32_t hid_usage = 0) {
  fuchsia::ui::input::KeyboardEvent keyboard_event{};
  keyboard_event.hid_usage = hid_usage;
  keyboard_event.phase = fuchsia::ui::input::KeyboardEventPhase::PRESSED;
  fuchsia::ui::input::InputEvent event{};
  event.set_keyboard(std::move(keyboard_event));
  return event;
}

TEST(InputDispatcherTest, EmptyQueue) {
  InputDispatcherImpl dispatcher(1);

  EXPECT_EQ(0u, dispatcher.Keyboard()->size());
  EXPECT_EQ(0u, dispatcher.Mouse()->size());
  EXPECT_EQ(0u, dispatcher.Touch()->size());
}

TEST(InputDispatcherTest, AddToQueue) {
  InputDispatcherImpl dispatcher(1);

  dispatcher.Keyboard()->PostEvent(fuchsia::ui::input::InputEvent{});
  dispatcher.Mouse()->PostEvent(fuchsia::ui::input::InputEvent{});
  dispatcher.Touch()->PostEvent(fuchsia::ui::input::InputEvent{});

  EXPECT_EQ(1u, dispatcher.Keyboard()->size());
  EXPECT_EQ(1u, dispatcher.Mouse()->size());
  EXPECT_EQ(1u, dispatcher.Touch()->size());
}

TEST(InputDispatcherTest, Overflow) {
  InputDispatcherImpl dispatcher(1);

  // Post 3 events with different HID codes. The oldest 2 will be dropped.
  dispatcher.Keyboard()->PostEvent(MakeKeyboardPressedEvent(1));
  dispatcher.Keyboard()->PostEvent(MakeKeyboardPressedEvent(2));
  dispatcher.Keyboard()->PostEvent(MakeKeyboardPressedEvent(3));

  // Verify event corresponds to the most recent one added.
  ASSERT_EQ(1u, dispatcher.Keyboard()->size());
  auto event = dispatcher.Keyboard()->Wait();
  EXPECT_EQ(3u, event.keyboard().hid_usage);
  EXPECT_EQ(0u, dispatcher.Keyboard()->size());
}

}  // namespace
}  // namespace machina
