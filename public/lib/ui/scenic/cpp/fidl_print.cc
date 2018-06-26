// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/scenic/cpp/fidl_print.h"

std::ostream& operator<<(std::ostream& str, const fuchsia::ui::gfx::vec2& vec) {
  return str << "(" << vec.x << ", " << vec.y << ")";
}

std::ostream& operator<<(std::ostream& str, const fuchsia::ui::gfx::vec3& vec) {
  return str << "(" << vec.x << ", " << vec.y << ", " << vec.z << ")";
}

std::ostream& operator<<(std::ostream& str, const fuchsia::ui::gfx::vec4& vec) {
  return str << "(" << vec.x << ", " << vec.y << ", " << vec.z << ", " << vec.w
             << ")";
}

std::ostream& operator<<(std::ostream& str,
                         const fuchsia::ui::gfx::BoundingBox& box) {
  return str << "BoundingBox(min" << box.min << ", max" << box.max << ")";
}

std::ostream& operator<<(std::ostream& str,
                         const fuchsia::ui::gfx::ViewProperties& props) {
  return str << "ViewProperties(" << props.extents << ", inset_from_min"
             << props.inset_from_min << ", inset_from_max"
             << props.inset_from_max << ")";
}
