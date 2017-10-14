// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/escher/geometry/types.h"

namespace escher {

// Directional light is emitted from a particular point at infinity.
//
// Although the light is directional, the light has some amount of angular
// dispersion (i.e., the light is not fully columnated). For simplicity, we
// assume the dispersion of the light source is symmetric about the light's
// primary direction.
class DirectionalLight {
 public:
  DirectionalLight();
  DirectionalLight(vec2 direction, float dispersion, float intensity);
  ~DirectionalLight();

  // The direction from which the light is received. The first coordinate is
  // theta (the the azimuthal angle, in radians) and the second coordinate is
  // phi (the polar angle, in radians).
  const vec2& direction() const { return direction_; }

  // The angular variance in the light, in radians.
  float dispersion() const { return dispersion_; }

  // The amount of light emitted.
  // TODO(abarth): In what units?
  float intensity() const { return intensity_; }

 private:
  vec2 direction_;
  float dispersion_ = 0.0f;
  float intensity_ = 0.0f;
};

}  // namespace escher
