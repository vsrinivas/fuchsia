// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/scene/shape.h"

#include "escher/geometry/tessellation.h"

namespace escher {

Shape::Shape(Type type) : type_(type) {}

Shape::~Shape() {}

Shape Shape::NewRect(const vec2& position, const vec2& size, float z) {
  Shape shape(Type::kRect);
  shape.position_ = position;
  shape.size_ = size;
  shape.z_ = z;
  return shape;
}

Shape Shape::NewCircle(const vec2& center, float radius, float z) {
  Shape shape(Type::kCircle);
  shape.position_ = vec2(center.x - radius, center.y - radius);
  shape.size_ = vec2(radius * 2.0f, radius * 2.0f);
  shape.z_ = z;
  return shape;
}

}  // namespace escher
