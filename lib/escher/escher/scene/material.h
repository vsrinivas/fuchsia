// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <glm/glm.hpp>

#include "escher/scene/binding.h"
#include "escher/scene/displacement.h"

namespace escher {

// Texture and shading properties to apply to a surface.
class Material {
 public:
  Material();
  ~Material();

  // Base color, default is none.
  void set_color(const Binding<glm::vec4>& color) { color_ = color; }
  const Binding<glm::vec4>& color() const { return color_; }

  enum class DisplacementType {
    kNone,
    kWave,
  };

  void set_displacement(const Displacement& displacement) {
    displacement_ = displacement;
  }
  const Displacement& displacement() const { return displacement_; }

  // TODO(jeffbrown): Normals.

 private:
  Binding<glm::vec4> color_;
  Displacement displacement_;
};

}  // namespace escher
