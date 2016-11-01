// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/scene/shape.h"

#include "escher/geometry/tessellation.h"

namespace escher {

Shape::Shape(Type type) : type_(type) {
  FTL_DCHECK(type != Type::kMesh);
}

Shape::Shape(MeshPtr mesh) : type_(Type::kMesh), mesh_(mesh) {
  FTL_DCHECK(mesh);
}

Shape::~Shape() {}

}  // namespace escher
