// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_SCENE_DIRECTIONAL_LIGHT_H_
#define SRC_UI_LIB_ESCHER_SCENE_DIRECTIONAL_LIGHT_H_

#include "src/ui/lib/escher/geometry/types.h"

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
  // Direction vector must be normalized.
  DirectionalLight(vec3 direction, float dispersion, vec3 color);

  // Direction is as described for the polar_direction() accessor.
  DirectionalLight(vec2 polar_direction, float dispersion, vec3 color);

  ~DirectionalLight();

  // The direction that the light travels.
  const vec3& direction() const { return direction_; }

  // The direction from which the light is received. The first coordinate is
  // theta (the azimuthal angle, in radians) and the second coordinate is
  // phi (the polar angle, in radians).
  const vec2& polar_direction() const { return polar_direction_; }

  // The angular variance in the light, in radians.
  // TODO(fxbug.dev/23754): it's not well-defined how rendering should/will respond to
  // this value. Its meaning is implicitly defined by implementation of
  // SsdoSampler, but it's not clear how/if it will be taken into account for
  // e.g. shadow-map-based soft shadows.
  float dispersion() const { return dispersion_; }

  // The amount of light emitted.
  // TODO(fxbug.dev/23755): In what units?
  const vec3& color() const { return color_; }
  void set_color(vec3 color) { color_ = color; }

  // TODO(fxbug.dev/23736): deprecated.  Only used for SSDO shadows, and white lights.
  float intensity() const { return color_.r; }

 private:
  vec3 direction_;
  vec2 polar_direction_;
  float dispersion_ = 0.0f;
  vec3 color_;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_SCENE_DIRECTIONAL_LIGHT_H_
