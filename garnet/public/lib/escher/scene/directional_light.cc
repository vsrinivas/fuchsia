// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/scene/directional_light.h"

#include <glm/gtc/epsilon.hpp>
#include <utility>

#include "lib/fxl/logging.h"

namespace escher {

DirectionalLight::DirectionalLight() {}

DirectionalLight::DirectionalLight(vec3 direction, float dispersion, vec3 color)
    : direction_(direction),
      polar_direction_(
          vec2(atan2(-direction.y, -direction.x), asin(-direction.z))),
      dispersion_(dispersion),
      color_(color) {
  if (polar_direction_.x < 0.f) {
    polar_direction_.x += 2 * M_PI;
  } else if (polar_direction_.x > 2 * M_PI) {
    polar_direction_.x -= 2 * M_PI;
  }
  FXL_DCHECK(glm::epsilonEqual(1.f, glm::length(direction), 0.0001f));
}

DirectionalLight::DirectionalLight(vec2 polar_direction, float dispersion,
                                   vec3 color)
    : polar_direction_(std::move(polar_direction)),
      dispersion_(dispersion),
      color_(color) {
  float xy_length = cos(polar_direction.y);
  direction_ =
      -vec3(xy_length * cos(polar_direction.x),
            xy_length * sin(polar_direction.x), sin(polar_direction.y));
}

DirectionalLight::~DirectionalLight() {}

}  // namespace escher
