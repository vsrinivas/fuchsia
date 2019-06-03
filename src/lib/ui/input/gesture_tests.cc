// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/ui/input/gesture.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace {

using fuchsia::ui::gfx::vec2;

class GestureTest : public testing::Test {
 protected:
  input::Gesture gesture_;
};

// Ensures that a single-pointer drag produces the expected deltas and no scale
// or rotation.
TEST_F(GestureTest, SinglePointerDrag) {
  gesture_.AddPointer(0, {0, 0});

  auto delta = gesture_.UpdatePointer(0, {1, 0});
  EXPECT_EQ(delta.translation, vec2({1, 0}));
  EXPECT_EQ(delta.rotation, 0);
  EXPECT_EQ(delta.scale, 1);

  delta = gesture_.UpdatePointer(0, {1, -1});
  EXPECT_EQ(delta.translation, vec2({0, -1}));
  EXPECT_EQ(delta.rotation, 0);
  EXPECT_EQ(delta.scale, 1);
}

// Ensures that after adding a new pointer, the delta is the average across
// both pointers and that the relative offset between the pointers does not
// skew the delta.
TEST_F(GestureTest, MultiPointerDelta) {
  gesture_.AddPointer(0, {1, 1});
  // move the first pointer to ensure no special treatment
  gesture_.UpdatePointer(0, {1, 2});

  gesture_.AddPointer(1, {10, 1});
  auto delta = gesture_.UpdatePointer(1, {10, 2});
  EXPECT_EQ(delta.translation, vec2({0, .5}));
}

// Basic 2-pointer scale.
TEST_F(GestureTest, Scale2) {
  gesture_.AddPointer(0, {0, 0});
  gesture_.AddPointer(1, {0, 1});

  auto delta = gesture_.UpdatePointer(1, {0, 2});
  EXPECT_EQ(delta.scale, 2);
  EXPECT_EQ(delta.rotation, 0);
}

// Ensures that 3-pointer scale is reasonable.
TEST_F(GestureTest, Scale3) {
  constexpr float kSqrt3 = -1.73f;

  gesture_.AddPointer(0, {0, -1});
  gesture_.AddPointer(1, {-kSqrt3, .5f});
  gesture_.AddPointer(2, {kSqrt3, .5f});

  input::Gesture::Delta delta;

  delta += gesture_.UpdatePointer(0, {0, -2});
  delta += gesture_.UpdatePointer(1, {2 * -kSqrt3, 1});
  delta += gesture_.UpdatePointer(2, {2 * kSqrt3, 1});

  EXPECT_NEAR(delta.scale, 2, .1f);
  EXPECT_NEAR(delta.rotation, 0, .05f);
  EXPECT_NEAR(delta.translation.x, 0, .01f);
  EXPECT_NEAR(delta.translation.y, 0, .01f);
}

TEST_F(GestureTest, Rotate2) {
  gesture_.AddPointer(0, {0, 0});
  gesture_.AddPointer(1, {0, 1});

  auto delta = gesture_.UpdatePointer(1, {.1f, 1});
  EXPECT_NEAR(delta.rotation, -.1f, .01f);
}

TEST_F(GestureTest, Rotate3) {
  constexpr float kSqrt3 = -1.73f;

  gesture_.AddPointer(0, {0, -1});
  gesture_.AddPointer(1, {-kSqrt3, .5f});
  gesture_.AddPointer(2, {kSqrt3, .5f});

  input::Gesture::Delta delta;

  delta += gesture_.UpdatePointer(0, {.1f, -1});
  delta += gesture_.UpdatePointer(1, {-kSqrt3 - .05f, .5f - .05f * kSqrt3});
  delta += gesture_.UpdatePointer(2, {kSqrt3 - .05f, .5f + .05f * kSqrt3});

  EXPECT_NEAR(delta.scale, 1, .01f);
  EXPECT_NEAR(delta.rotation, .1f, .05f);
  EXPECT_NEAR(delta.translation.x, 0, .01f);
  EXPECT_NEAR(delta.translation.y, 0, .01f);
}

TEST_F(GestureTest, RemovePointer) {
  EXPECT_FALSE(gesture_.has_pointers());
  EXPECT_EQ(gesture_.pointer_count(), 0u);

  gesture_.AddPointer(0, {0, 0});
  EXPECT_TRUE(gesture_.has_pointers());
  EXPECT_EQ(gesture_.pointer_count(), 1u);

  gesture_.AddPointer(1, {0, 1});
  EXPECT_TRUE(gesture_.has_pointers());
  EXPECT_EQ(gesture_.pointer_count(), 2u);

  // move both pointers to ensure no special treatment
  gesture_.UpdatePointer(0, {1, 0});
  gesture_.UpdatePointer(1, {1, 1});

  EXPECT_TRUE(gesture_.has_pointers());
  EXPECT_EQ(gesture_.pointer_count(), 2u);

  gesture_.RemovePointer(0);
  EXPECT_TRUE(gesture_.has_pointers());
  EXPECT_EQ(gesture_.pointer_count(), 1u);

  auto delta = gesture_.UpdatePointer(1, {1, 2});
  EXPECT_EQ(delta,
            input::Gesture::Delta(
                {.translation = vec2({0, 1}), .rotation = 0, .scale = 1}));
  EXPECT_TRUE(gesture_.has_pointers());
  EXPECT_EQ(gesture_.pointer_count(), 1u);

  gesture_.RemovePointer(1);
  EXPECT_FALSE(gesture_.has_pointers());
  EXPECT_EQ(gesture_.pointer_count(), 0u);
}

}  // namespace
