// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_SCENE_DISPLACEMENT_H_
#define LIB_ESCHER_SCENE_DISPLACEMENT_H_

#include <math.h>

#include "lib/escher/geometry/types.h"
#include "lib/escher/scene/binding.h"

namespace escher {

// Texture and shading properties to apply to a surface.
class Displacement {
 public:
  enum class Type {
    kNone,
    kWave,
    // TODO(abarth): The client should be able to use a texture.
  };

  Displacement();
  ~Displacement();

  static Displacement MakeWave(const vec2& start, const vec2& end, float max,
                               float theta_min = -M_PI, float theta_max = M_PI);

  Type type() const { return type_; }
  float max() const { return max_; }

  // For Type::kWave.
  vec2 start() const { return vec2(parameters_.x, parameters_.y); }
  vec2 end() const { return vec2(parameters_.z, parameters_.w); }
  float theta_min() const { return theta_min_; }
  float theta_max() const { return theta_max_; }

 private:
  Type type_ = Type::kNone;
  vec4 parameters_;
  float max_ = 0.0f;
  float theta_min_ = 0.0f;
  float theta_max_ = 0.0f;
};

}  // namespace escher

#endif  // LIB_ESCHER_SCENE_DISPLACEMENT_H_
