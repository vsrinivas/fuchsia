// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/scene/object.h"

namespace escher {

Object::Object(const Transform& transform, MeshPtr mesh, MaterialPtr material)
    : Object(static_cast<mat4>(transform), std::move(mesh),
             std::move(material)) {}

Object::Object(const mat4& transform, MeshPtr mesh, MaterialPtr material)
    : Object(transform, Shape(std::move(mesh)), std::move(material)) {}

Object::Object(const vec3& position, MeshPtr mesh, MaterialPtr material)
    : Object(glm::translate(position), std::move(mesh), std::move(material)) {}

Object::Object(std::vector<Object> clippers, std::vector<Object> clippees)
    : transform_(mat4(1)),
      shape_(Shape(Shape::Type::kNone)),
      material_(MaterialPtr()),
      clippers_(std::move(clippers)),
      clippees_(std::move(clippees)) {}

Object::~Object() {}

Object Object::NewRect(const vec2& top_left_position, const vec2& size, float z,
                       MaterialPtr material) {
  return NewRect(vec3(top_left_position, z), size, std::move(material));
}

Object Object::NewRect(const vec3& top_left_position, const vec2& size,
                       MaterialPtr material) {
  mat4 transform(1);
  transform[0][0] = size.x;
  transform[1][1] = size.y;
  transform[3][0] = top_left_position.x;
  transform[3][1] = top_left_position.y;
  transform[3][2] = top_left_position.z;
  return Object(transform, Shape(Shape::Type::kRect), std::move(material));
}

Object Object::NewRect(const Transform& transform, MaterialPtr material) {
  return Object(static_cast<mat4>(transform), Shape(Shape::Type::kRect),
                std::move(material));
}

Object Object::NewRect(const mat4& transform, MaterialPtr material) {
  return Object(transform, Shape(Shape::Type::kRect), std::move(material));
}

Object Object::NewCircle(const vec2& center_position, float radius, float z,
                         MaterialPtr material) {
  return NewCircle(vec3(center_position, z), radius, std::move(material));
}

Object Object::NewCircle(const vec3& center_position, float radius,
                         MaterialPtr material) {
  mat4 transform(1);
  transform[0][0] = radius;
  transform[1][1] = radius;
  transform[3][0] = center_position.x;
  transform[3][1] = center_position.y;
  transform[3][2] = center_position.z;
  return Object(transform, Shape(Shape::Type::kCircle), std::move(material));
}

Object Object::NewCircle(const mat4& transform, float radius,
                         MaterialPtr material) {
  return Object(glm::scale(transform, glm::vec3(radius, radius, 1)),
                Shape(Shape::Type::kCircle), std::move(material));
}

BoundingBox Object::bounding_box() const {
  BoundingBox box = shape_.bounding_box();
  for (auto& clipper : clippers_) {
    box.Join(clipper.bounding_box());
  }
  // Don't intersect with the clippers, because we do not know the camera
  // viewpoint.
  return transform_ * box;
}

}  // namespace escher
