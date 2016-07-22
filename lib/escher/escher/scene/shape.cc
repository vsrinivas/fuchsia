// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/scene/shape.h"

#include "escher/geometry/tessellation.h"

namespace escher {

Shape::Shape(Type type) : type_(type) {}

Shape::~Shape() {}

Shape Shape::CreateRect(const vec2& position,
                        const vec2& size,
                        float z) {
  Shape shape(Type::kRect);
  shape.position_ = position;
  shape.size_ = size;
  shape.z_ = z;
  return shape;
}

Shape Shape::CreateCircle(const vec2& center, float radius, float z) {
  Shape shape(Type::kCircle);
  shape.position_ = vec2(center.x - radius, center.y - radius);
  shape.size_ = vec2(radius * 2.0f, radius * 2.0f);
  shape.z_ = z;
  return shape;
}

Shape Shape::CreateMesh(
    const Tessellation& tessellation, vec2 position, float z) {
  return CreateMesh(ftl::MakeRefCounted<Mesh>(tessellation), position, z);
}

Shape Shape::CreateMesh(ftl::RefPtr<Mesh> mesh, vec2 position, float z) {
  Shape shape(Type::kMesh);
  shape.position_ = position;
  shape.z_ = z;
  shape.mesh_ = std::move(mesh);
  return shape;
}

}  // namespace escher
