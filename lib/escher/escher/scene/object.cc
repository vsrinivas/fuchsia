// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/scene/object.h"

namespace escher {

Object::Object(const Shape& shape, const MaterialPtr& material)
    : shape_(shape), material_(material) {}

Object::~Object() {}

Object Object::NewRect(const vec2& position,
                       const vec2& size,
                       float z,
                       const MaterialPtr& material) {
  Object obj(Shape(Shape::Type::kRect), material);
  obj.position_ = vec3(position, z);
  obj.size_ = size;
  return obj;
}

Object Object::NewCircle(const vec2& center,
                         float radius,
                         float z,
                         const MaterialPtr& material) {
  Object obj(Shape(Shape::Type::kCircle), material);
  obj.position_ = vec3(center.x - radius, center.y - radius, z);
  obj.size_ = vec2(radius * 2.f, radius * 2.f);
  return obj;
}

}  // namespace escher
