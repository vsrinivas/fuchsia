// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <glm/glm.hpp>

#include "escher/scene/material.h"
#include "escher/scene/shape.h"

namespace escher {

// An object instance to be drawn using a shape and a material.
// Does not retain ownership of the material.
class Object {
 public:
  Object(const Shape& shape, const Material* material);
  ~Object();

  // The shape to draw.
  const Shape& shape() const { return shape_; }

  // The material with which to fill the shape.
  const Material* material() const { return material_; }

 private:
  Shape shape_;
  const Material* material_;
};

}  // namespace escher
