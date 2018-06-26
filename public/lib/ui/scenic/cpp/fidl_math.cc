// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/scenic/cpp/fidl_math.h"

namespace scenic {

bool ContainsPoint(const fuchsia::ui::gfx::BoundingBox& box,
                   const fuchsia::ui::gfx::vec3& point) {
  return point.x >= box.min.x && point.y >= box.min.y && point.z >= box.min.z &&
         point.x <= box.max.x && point.y <= box.max.y && point.z <= box.max.z;
}

fuchsia::ui::gfx::BoundingBox InsetBy(const fuchsia::ui::gfx::BoundingBox& box,
                                      const fuchsia::ui::gfx::vec3& inset) {
  return {.min = box.min + inset, .max = box.max - inset};
}

fuchsia::ui::gfx::BoundingBox InsetBy(
    const fuchsia::ui::gfx::BoundingBox& box,
    const fuchsia::ui::gfx::vec3& inset_from_min,
    const fuchsia::ui::gfx::vec3& inset_from_max) {
  return {.min = box.min + inset_from_min, .max = box.max - inset_from_max};
}

fuchsia::ui::gfx::BoundingBox ViewPropertiesLayoutBox(
    const fuchsia::ui::gfx::ViewProperties& view_properties) {
  return InsetBy(view_properties.extents, view_properties.inset_from_min,
                 view_properties.inset_from_max);
}

}  // namespace scenic
