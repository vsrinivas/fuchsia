// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/shapes/shape.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo Shape::kTypeInfo = {ResourceType::kShape, "Shape"};

Shape::Shape(Session* session, SessionId session_id, ResourceId id,
             const ResourceTypeInfo& type_info)
    : Resource(session, session_id, id, type_info) {
  FX_DCHECK(type_info.IsKindOf(Shape::kTypeInfo));
}

}  // namespace gfx
}  // namespace scenic_impl
