// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <glm/glm.hpp>

#include "escher/material/material.h"
#include "escher/scene/shape.h"

namespace escher {

// An object instance to be drawn using a shape and a material.
// Does not retain ownership of the material.
class Object {
 public:
  ~Object();

  // The shape to draw.
  const Shape& shape() const { return shape_; }

  // The material with which to fill the shape.
  const Material* material() const { return material_; }

  static Object NewRect(const vec2& position,
                        const vec2& size,
                        float z,
                        const Material* material);
  static Object NewCircle(const vec2& center,
                          float radius,
                          float z,
                          const Material* material);

  const vec3& color() const { return color_; }
  void set_color(const vec3& color) { color_ = color; }

  float width() const { return size_.x; }
  float height() const { return size_.y; }
  const vec3& position() const { return position_; }

 private:
  Object(const Shape& shape, const Material* material);

  Shape shape_;
  const Material* material_;
  vec3 position_;
  vec2 size_;
  vec3 color_;
};

}  // namespace escher
