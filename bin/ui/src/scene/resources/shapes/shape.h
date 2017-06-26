// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/src/scene/resources/resource.h"
#include "escher/geometry/types.h"
#include "escher/scene/object.h"

namespace mozart {
namespace scene {

class Shape : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  /// Returns if the given point lies within its bounds of this shape.
  virtual bool ContainsPoint(const escher::vec2& point) const = 0;

  virtual escher::Object GenerateRenderObject(
      const escher::mat4& transform,
      const escher::MaterialPtr& material) = 0;

 protected:
  Shape(Session* session, const ResourceTypeInfo& type_info);
};

using ShapePtr = ftl::RefPtr<Shape>;

}  // namespace scene
}  // namespace mozart
