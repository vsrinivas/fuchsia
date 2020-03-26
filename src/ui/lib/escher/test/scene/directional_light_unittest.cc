// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/scene/directional_light.h"

#include <gtest/gtest.h>

#include <glm/gtc/epsilon.hpp>

namespace {
using namespace escher;

TEST(DirectionalLight, PolarAndVectorConstructorsMatch) {
  constexpr float kDispersion = 1.f;
  const vec3 kColor(1.f, 1.f, 1.f);
  constexpr float kStepSize = 0.01f;
  constexpr float kEpsilon = 0.00002f;

  constexpr float kNearNorthPoleElevation = M_PI / 2 - 0.0002f;
  constexpr float kNearSouthPoleElevation = -kNearNorthPoleElevation;

  for (float azimuth = 0; azimuth < 2 * M_PI; azimuth += kStepSize) {
    for (float elevation = -M_PI / 2; elevation <= M_PI / 2; elevation += kStepSize) {
      DirectionalLight polar1(vec2(azimuth, elevation), kDispersion, kColor);
      DirectionalLight euclid1(polar1.direction(), kDispersion, kColor);
      DirectionalLight polar2(vec2(euclid1.polar_direction()), kDispersion, kColor);
      DirectionalLight euclid2(polar2.direction(), kDispersion, kColor);

      EXPECT_NEAR(0.f, glm::distance(polar1.direction(), euclid2.direction()), kEpsilon);

      // Near the poles there are precision issues with atan2() that cause the
      // azimuth to differ wildly; as long as the Euclidean direction vectors
      // are close enough, we're happy.
      if (elevation >= kNearSouthPoleElevation && elevation < kNearNorthPoleElevation) {
        EXPECT_NEAR(0.f, glm::distance(polar1.polar_direction(), euclid2.polar_direction()),
                    kEpsilon);
      }
    }
  }
}

}  // namespace
