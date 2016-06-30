// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <glm/glm.hpp>

#include "escher/base/macros.h"

namespace escher {

// Describes a planar shape primitive to be drawn.
class Shape {
 public:
  enum class Type { kRect, kCircle };

  ~Shape();

  static Shape CreateRect(const glm::vec2& position,
                          const glm::vec2& size,
                          float z);
  static Shape CreateCircle(const glm::vec2& center, float radius, float z);

  // TODO(jeffbrown): CreateMesh (with bounding box?)

  Type type() const { return type_; }

  // Top-left coordinate in pixels.
  const glm::vec2& position() const { return position_; }

  // Size in pixels.
  const glm::vec2& size() const { return size_; }

  // Elevation in pixels.
  float z() const { return z_; }

  // Radius in pixels.
  float radius() const {
    ESCHER_DCHECK(type_ == Type::kCircle);
    return size_.x * 0.5f;
  }

 private:
  explicit Shape(Type type);

  Type type_;
  glm::vec2 position_;
  glm::vec2 size_;
  float z_ = 0;
};

}  // namespace escher
