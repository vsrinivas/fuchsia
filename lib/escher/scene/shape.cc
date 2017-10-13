// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/scene/shape.h"

#include "escher/geometry/tessellation.h"

namespace escher {

Shape::Shape(Type type, ShapeModifiers modifiers)
    : type_(type), modifiers_(modifiers) {
  FXL_DCHECK(type != Type::kMesh);
  if (modifiers_ & ShapeModifier::kWobble) {
    FXL_LOG(ERROR) << "ShapeModifier::kWobble only supported for kMesh shapes.";
    FXL_CHECK(false);
  }
}

Shape::Shape(MeshPtr mesh, ShapeModifiers modifiers)
    : type_(Type::kMesh), modifiers_(modifiers), mesh_(mesh) {
  FXL_DCHECK(mesh);
  if (modifiers_ & ShapeModifier::kWobble) {
    const MeshAttributes required =
        MeshAttribute::kPositionOffset | MeshAttribute::kPerimeterPos;
    if (required != (mesh_->spec().flags & required)) {
      FXL_LOG(ERROR) << "ShapeModifier::kWobble requires both kPositionOffset "
                        "and kPerimeterPos";
      FXL_CHECK(false);
    }
  }
}

Shape::~Shape() {}

BoundingBox Shape::bounding_box() const {
  switch (type_) {
    case Type::kRect:
      return BoundingBox({0, 0, 0}, {1, 1, 0});
    case Type::kCircle:
      return BoundingBox({-1, -1, 0}, {1, 1, 0});
    case Type::kMesh:
      return mesh_->bounding_box();
    case Type::kNone:
      return BoundingBox();
  }
}

void Shape::set_mesh(MeshPtr mesh) {
  FXL_DCHECK(type_ == Type::kMesh);
  mesh_ = std::move(mesh);
}

}  // namespace escher
