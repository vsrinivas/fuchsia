// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/scene/shape.h"

#include "src/ui/lib/escher/mesh/tessellation.h"

namespace escher {

Shape::Shape(Type type) : type_(type) { FXL_DCHECK(type != Type::kMesh); }

Shape::Shape(MeshPtr mesh) : type_(Type::kMesh), mesh_(mesh) { FXL_DCHECK(mesh); }

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
