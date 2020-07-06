// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/gesture_listener_registry.h"

#include <fuchsia/accessibility/gesture/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_n_tap_recognizer.h"
#include "src/ui/a11y/lib/gesture_manager/tests/mocks/mock_gesture_listener.h"
#include "src/ui/a11y/lib/testing/input.h"

namespace accessibility_test {
namespace {

class GestureListenerRegistryTest : public gtest::TestLoopFixture {
 public:
  GestureListenerRegistryTest() = default;
  ~GestureListenerRegistryTest() override = default;

  a11y::GestureListenerRegistry registry_;
};

TEST_F(GestureListenerRegistryTest, RegistersSuccessfully) {
  EXPECT_FALSE(registry_.listener());
  MockGestureListener listener;
  registry_.Register(listener.NewBinding(), []() {});
  EXPECT_TRUE(registry_.listener());
}

TEST_F(GestureListenerRegistryTest, HonorsLastRegisteredListener) {
  EXPECT_FALSE(registry_.listener());
  MockGestureListener listener;
  MockGestureListener last_listener;
  registry_.Register(listener.NewBinding(), []() {});
  registry_.Register(last_listener.NewBinding(), []() {});
  RunLoopUntilIdle();
  EXPECT_TRUE(registry_.listener());
  EXPECT_TRUE(last_listener.is_registered());
  EXPECT_FALSE(listener.is_registered());
}

}  // namespace
}  // namespace accessibility_test
