// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/input_dispatcher.h"
#include "gtest/gtest.h"

namespace machina {
namespace {

TEST(InputDispatcherTest, EmptyQueue) {
  InputDispatcher dispatcher(1);

  EXPECT_EQ(0u, dispatcher.Keyboard()->size());
  EXPECT_EQ(0u, dispatcher.Pointer()->size());
}

TEST(InputDispatcherTest, AddToQueue) {
  InputDispatcher dispatcher(1);

  InputEvent event = {};
  dispatcher.Keyboard()->PostEvent(event);
  dispatcher.Pointer()->PostEvent(event);

  EXPECT_EQ(1u, dispatcher.Keyboard()->size());
  EXPECT_EQ(1u, dispatcher.Pointer()->size());
}

TEST(InputDispatcherTest, Overflow) {
  InputDispatcher dispatcher(1);
  InputEvent event;
  event.type = InputEventType::KEYBOARD;
  event.key.hid_usage = 0;
  event.key.state = KeyState::PRESSED;

  // Post 3 events with differnt HID codes. The oldest 2 will be dropped.
  event.key.hid_usage = 1;
  dispatcher.Keyboard()->PostEvent(event);
  event.key.hid_usage = 2;
  dispatcher.Keyboard()->PostEvent(event);
  event.key.hid_usage = 3;
  dispatcher.Keyboard()->PostEvent(event);

  // Verify event corresponds to the most recent one added.
  event = {};
  ASSERT_EQ(1u, dispatcher.Keyboard()->size());
  event = dispatcher.Keyboard()->Wait();
  EXPECT_EQ(3u, event.key.hid_usage);
  EXPECT_EQ(0u, dispatcher.Keyboard()->size());
}

}  // namespace
}  // namespace machina
