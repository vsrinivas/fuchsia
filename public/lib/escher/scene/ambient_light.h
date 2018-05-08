// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_SCENE_AMBIENT_LIGHT_H_
#define LIB_ESCHER_SCENE_AMBIENT_LIGHT_H_

#include "lib/escher/geometry/types.h"

namespace escher {

// Ambient light is emitted omnidirectionally from infinity.
class AmbientLight {
 public:
  explicit AmbientLight(float intensity = 0.f);
  explicit AmbientLight(const vec3& color);
  ~AmbientLight();

  // The amount of light emitted.
  // TODO(abarth): In what units?
  // TODO(jjosh): deprecated.  Only used for SSDO shadows, and white lights.
  float intensity() const { return color_.r; }

  const vec3 color() const { return color_; }

 private:
  vec3 color_;
};

}  // namespace escher

#endif  // LIB_ESCHER_SCENE_AMBIENT_LIGHT_H_
