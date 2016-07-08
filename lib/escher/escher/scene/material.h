// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <glm/mat2x2.hpp>
#include <glm/vec4.hpp>

#include "escher/gl/bindings.h"
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

  void set_texture(GLuint texture) {
    texture_ = texture;
  }
  GLuint texture() const { return texture_; }
  bool has_texture() const { return texture_ != 0; }

  void set_texture_matrix(const Binding<glm::mat2>& texture_matrix) {
    texture_matrix_ = texture_matrix;
  }
  const Binding<glm::mat2>& texture_matrix() const { return texture_matrix_; }

  // TODO(jeffbrown): Normals.

 private:
  Binding<glm::vec4> color_;
  GLuint texture_ = 0;
  Binding<glm::mat2> texture_matrix_;
  Displacement displacement_;
};

}  // namespace escher
