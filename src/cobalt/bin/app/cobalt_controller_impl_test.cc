// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/cobalt_controller_impl.h"

#include <gtest/gtest.h>

namespace cobalt {

// If ListenForInitialized() is invoked before OnSystemClockBecomesAccurate()
// then the callback will not be immediately invoked. But it will later be
// invoked when OnSystemClockBecomesAccurate() is invoked.
TEST(ListenForInitialized, ListenBeforeAccurate) {
  CobaltControllerImpl controller_impl(nullptr, nullptr);
  fuchsia::cobalt::Controller* controller = &controller_impl;
  bool callback_invoked = false;
  controller->ListenForInitialized([&callback_invoked]() { callback_invoked = true; });
  EXPECT_FALSE(callback_invoked);
  controller_impl.OnSystemClockBecomesAccurate();
  EXPECT_TRUE(callback_invoked);
}

// If ListenForInitialized() is invoked after OnSystemClockBecomesAccurate()
// is invoked then the callback is immediately invoked.
TEST(ListenForInitialized, ListenAfterAccurate) {
  CobaltControllerImpl controller_impl(nullptr, nullptr);
  fuchsia::cobalt::Controller* controller = &controller_impl;
  bool callback_invoked = false;
  controller_impl.OnSystemClockBecomesAccurate();
  controller->ListenForInitialized([&callback_invoked]() { callback_invoked = true; });
  EXPECT_TRUE(callback_invoked);
}

// Like ListenBeforeAccurate but with many invocatoins of
// ListenForInitialized().
TEST(ListenForInitialized, ListenBeforeAccurateMany) {
  CobaltControllerImpl controller_impl(nullptr, nullptr);
  fuchsia::cobalt::Controller* controller = &controller_impl;
  static const size_t kNumCallbacks = 10;
  bool callback_invoked[kNumCallbacks] = {false};
  for (auto i = 0; i < kNumCallbacks; i++) {
    controller->ListenForInitialized([&invoked = callback_invoked[i]]() { invoked = true; });
  }
  for (auto i = 0; i < kNumCallbacks; i++) {
    EXPECT_FALSE(callback_invoked[i]);
  }
  controller_impl.OnSystemClockBecomesAccurate();
  for (auto i = 0; i < kNumCallbacks; i++) {
    EXPECT_TRUE(callback_invoked[i]);
  }
}

// Like ListenAfterAccurate but with many invocatoins of
// ListenForInitialized().
TEST(ListenForInitialized, ListenAfterAccurateMany) {
  CobaltControllerImpl controller_impl(nullptr, nullptr);
  fuchsia::cobalt::Controller* controller = &controller_impl;
  controller_impl.OnSystemClockBecomesAccurate();
  for (auto i = 0; i < 10; i++) {
    bool callback_invoked = false;
    controller->ListenForInitialized([&callback_invoked]() { callback_invoked = true; });
    EXPECT_TRUE(callback_invoked);
  }
}

// Combines all of the previous cases into one more complicated situation
// with multiple invocations of OnSystemClockBecomesAccurate().
TEST(ListenForInitialized, MultipleInterleaved) {
  CobaltControllerImpl controller_impl(nullptr, nullptr);
  fuchsia::cobalt::Controller* controller = &controller_impl;
  static const size_t kNumCallbacks = 10;
  bool callback_invoked[kNumCallbacks] = {false};
  for (auto i = 0; i < kNumCallbacks; i++) {
    controller->ListenForInitialized([&invoked = callback_invoked[i]]() { invoked = true; });
  }
  for (auto i = 0; i < kNumCallbacks; i++) {
    EXPECT_FALSE(callback_invoked[i]);
  }
  controller_impl.OnSystemClockBecomesAccurate();
  controller_impl.OnSystemClockBecomesAccurate();
  controller_impl.OnSystemClockBecomesAccurate();
  for (auto i = 0; i < kNumCallbacks; i++) {
    EXPECT_TRUE(callback_invoked[i]);
  }
  for (auto i = 0; i < 10; i++) {
    bool callback_invoked = false;
    controller->ListenForInitialized([&callback_invoked]() { callback_invoked = true; });
    EXPECT_TRUE(callback_invoked);
  }
  controller_impl.OnSystemClockBecomesAccurate();
  controller_impl.OnSystemClockBecomesAccurate();
  controller_impl.OnSystemClockBecomesAccurate();
  for (auto i = 0; i < 10; i++) {
    bool callback_invoked = false;
    controller->ListenForInitialized([&callback_invoked]() { callback_invoked = true; });
    EXPECT_TRUE(callback_invoked);
  }
}

}  // namespace cobalt
