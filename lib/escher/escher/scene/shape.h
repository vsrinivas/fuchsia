// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/geometry/types.h"
#include "escher/shape/mesh.h"

#include "ftl/logging.h"

namespace escher {

// Describes a planar shape primitive to be drawn.
class Shape {
 public:
  enum class Type { kRect, kCircle, kMesh };

  explicit Shape(Type type);
  explicit Shape(MeshPtr mesh);
  ~Shape();

  Type type() const { return type_; }

  const MeshPtr& mesh() const {
    FTL_DCHECK(type_ == Type::kMesh);
    return mesh_;
  }

 private:
  Type type_;
  MeshPtr mesh_;
};

}  // namespace escher
