// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_SCENE_SHAPE_H_
#define SRC_UI_LIB_ESCHER_SCENE_SHAPE_H_

#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/lib/escher/scene/shape_modifier.h"
#include "src/ui/lib/escher/shape/mesh.h"
#include "src/ui/lib/escher/util/debug_print.h"

#include "src/lib/fxl/logging.h"

namespace escher {

// Describes a planar shape primitive to be drawn.
class Shape {
 public:
  enum class Type { kRect, kCircle, kMesh, kNone };

  explicit Shape(Type type, ShapeModifiers modifiers = ShapeModifiers());
  explicit Shape(MeshPtr mesh, ShapeModifiers modifiers = ShapeModifiers());
  ~Shape();

  Type type() const { return type_; }
  ShapeModifiers modifiers() const { return modifiers_; }
  void set_mesh(MeshPtr mesh);
  void set_modifiers(ShapeModifiers modifiers) { modifiers_ = modifiers; }
  void remove_modifier(ShapeModifier modifier) { modifiers_ &= ~modifier; }

  const MeshPtr& mesh() const {
    FXL_DCHECK(type_ == Type::kMesh);
    return mesh_;
  }

  BoundingBox bounding_box() const;

 private:
  Type type_;
  ShapeModifiers modifiers_;
  MeshPtr mesh_;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_SCENE_SHAPE_H_
