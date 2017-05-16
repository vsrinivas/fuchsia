// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/composer/resources/shapes/shape.h"

namespace mozart {
namespace composer {

const ResourceTypeInfo Shape::kTypeInfo = {ResourceType::kShape, "Shape"};

Shape::Shape(Session* session, const ResourceTypeInfo& type_info)
    : Resource(session, type_info) {
  FTL_DCHECK(type_info.IsKindOf(Shape::kTypeInfo));
}

}  // namespace composer
}  // namespace mozart
