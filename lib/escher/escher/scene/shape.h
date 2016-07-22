// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/geometry/types.h"
#include "escher/gl/mesh.h"

#include "ftl/logging.h"

namespace escher {

struct Tessellation;

// Describes a planar shape primitive to be drawn.
class Shape {
 public:
  enum class Type { kRect, kCircle, kMesh };

  ~Shape();

  static Shape CreateRect(const vec2& position, const vec2& size, float z);
  static Shape CreateCircle(const vec2& center, float radius, float z);
  static Shape CreateMesh(
      const Tessellation& tesselation, vec2 position, float z);
  static Shape CreateMesh(ftl::RefPtr<Mesh> mesh, vec2 position, float z);

  // TODO(jeffbrown): CreateMesh (with bounding box?)

  Type type() const { return type_; }

  // Top-left coordinate in pixels.
  const vec2& position() const { return position_; }

  // Size in pixels.
  const vec2& size() const { return size_; }

  // Elevation in pixels.
  float z() const { return z_; }

  // Radius in pixels.
  float radius() const {
    FTL_DCHECK(type_ == Type::kCircle);
    return size_.x * 0.5f;
  }

  const Mesh& mesh() const {
    FTL_DCHECK(type_ == Type::kMesh);
    return *mesh_.get();
  }

 private:
  explicit Shape(Type type);
  Shape(Type type, vec2 position, vec2 size);

  Type type_;
  vec2 position_;
  vec2 size_;
  float z_ = 0;
  ftl::RefPtr<Mesh> mesh_;
};

}  // namespace escher
