// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/magnifier/magnifier_util.h"

#include <gtest/gtest.h>

namespace accessibility_test {
namespace {

TEST(MagnifierUtilTest, DeltaSum) {
  a11y::Delta delta_1;
  delta_1.translation = glm::vec2(1, 2);
  delta_1.scale = 2;

  a11y::Delta delta_2;
  delta_2.translation = glm::vec2(3, 4);
  delta_2.scale = .5f;

  delta_1 += delta_2;
  EXPECT_EQ(delta_1.translation.x, 4);
  EXPECT_EQ(delta_1.translation.y, 6);
  EXPECT_EQ(delta_1.scale, 1);
}

TEST(MagnifierUnitTest, GetDeltaFromGestureContexts) {
  // Current pointer locations are:
  //   Poinetr 0: (6, 8)
  //   Pointer 1: (0, 0)
  //   Centroid: (3, 4)
  // Previous pointer locations are:
  //   Pointer 0: (9, 13)
  //   Pionter 1: (-3, -3)
  //   Centroid: (3, 5)
  // NOTE: NDC coordinates will be between -1 and 1, but we use integers here to
  // avoid flakiness from float rounding.
  a11y::GestureContext current;
  current.current_pointer_locations[0].ndc_point.x = 6;
  current.current_pointer_locations[0].ndc_point.y = 8;
  current.current_pointer_locations[1].ndc_point.x = 0;
  current.current_pointer_locations[1].ndc_point.y = 0;

  a11y::GestureContext previous;
  previous.current_pointer_locations[0].ndc_point.x = 9;
  previous.current_pointer_locations[0].ndc_point.y = 13;
  previous.current_pointer_locations[1].ndc_point.x = -3;
  previous.current_pointer_locations[1].ndc_point.y = -3;

  auto delta = GetDelta(current, previous);

  EXPECT_EQ(delta.translation.x, 0);
  EXPECT_EQ(delta.translation.y, -1);
  EXPECT_EQ(delta.scale, .5f);
}

TEST(MagnifierUnitTest, GetDeltaFromGestureContextsDifferentNumPointers) {
  a11y::GestureContext current;
  current.current_pointer_locations[0].ndc_point.x = 6;
  current.current_pointer_locations[0].ndc_point.y = 8;

  a11y::GestureContext previous;

  auto delta = GetDelta(current, previous);

  EXPECT_EQ(delta.translation.x, 0);
  EXPECT_EQ(delta.translation.y, 0);
  EXPECT_EQ(delta.scale, 1);
}

TEST(MagnifierUnitTest, GetDeltaFromGestureContextsDifferentPointerIds) {
  a11y::GestureContext current;
  current.current_pointer_locations[0].ndc_point.x = 6;
  current.current_pointer_locations[0].ndc_point.y = 8;

  a11y::GestureContext previous;
  previous.current_pointer_locations[1].ndc_point.x = -3;
  previous.current_pointer_locations[1].ndc_point.y = -3;

  auto delta = GetDelta(current, previous);

  EXPECT_EQ(delta.translation.x, 0);
  EXPECT_EQ(delta.translation.y, 0);
  EXPECT_EQ(delta.scale, 1);
}

}  // namespace
}  // namespace accessibility_test
