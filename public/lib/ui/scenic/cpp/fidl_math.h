// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_SCENIC_CPP_FIDL_MATH_H_
#define LIB_UI_SCENIC_CPP_FIDL_MATH_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>

// TODO(SCN-811): add tests.

namespace scenic {

// Return true if |point| is contained by |box|, including when it is on the
// box boundary, and false otherwise.
bool ContainsPoint(const fuchsia::ui::gfx::BoundingBox& box,
                   const fuchsia::ui::gfx::vec3& point);

// Similar to fuchsia::ui::gfx::ViewProperties: adds the inset to box.min, and
// subtracts it from box.max.
fuchsia::ui::gfx::BoundingBox InsetBy(const fuchsia::ui::gfx::BoundingBox& box,
                                      const fuchsia::ui::gfx::vec3& inset);

// Similar to fuchsia::ui::gfx::ViewProperties: adds the inset to box.min, and
// subtracts it from box.max.
fuchsia::ui::gfx::BoundingBox InsetBy(
    const fuchsia::ui::gfx::BoundingBox& box,
    const fuchsia::ui::gfx::vec3& inset_from_min,
    const fuchsia::ui::gfx::vec3& inset_from_max);

// Inset the view properties' outer box by its insets.
fuchsia::ui::gfx::BoundingBox ViewPropertiesLayoutBox(
    const fuchsia::ui::gfx::ViewProperties& view_properties);

// Return a vec3 consisting of the maximum x/y/z from the two arguments.
inline fuchsia::ui::gfx::vec3 Max(const fuchsia::ui::gfx::vec3& a,
                                  const fuchsia::ui::gfx::vec3& b) {
  return {.x = std::max(a.x, b.x),
          .y = std::max(a.y, b.y),
          .z = std::max(a.z, b.z)};
}

// Return a vec3 consisting of the maximum of the x/y/z components of |v|,
// compared with |min_val|.
inline fuchsia::ui::gfx::vec3 Max(const fuchsia::ui::gfx::vec3& v,
                                  float min_val) {
  return {.x = std::max(v.x, min_val),
          .y = std::max(v.y, min_val),
          .z = std::max(v.z, min_val)};
}

// Return a vec3 consisting of the minimum x/y/z from the two arguments.
inline fuchsia::ui::gfx::vec3 Min(const fuchsia::ui::gfx::vec3& a,
                                  const fuchsia::ui::gfx::vec3& b) {
  return {.x = std::min(a.x, b.x),
          .y = std::min(a.y, b.y),
          .z = std::min(a.z, b.z)};
}

// Return a vec3 consisting of the minimum of the x/y/z components of |v|,
// compared with |max_val|.
inline fuchsia::ui::gfx::vec3 Min(const fuchsia::ui::gfx::vec3& v,
                                  float max_val) {
  return {.x = std::min(v.x, max_val),
          .y = std::min(v.y, max_val),
          .z = std::min(v.z, max_val)};
}

}  // namespace scenic

namespace fuchsia {
namespace ui {
namespace gfx {

// Return a vec2 consisting of the component-wise sum of the two arguments.
inline vec2 operator+(const vec2& a, const vec2& b) {
  return {.x = a.x + b.x, .y = a.y + b.y};
}

// Return a vec2 consisting of the component-wise difference of the two args.
inline vec2 operator-(const vec2& a, const vec2& b) {
  return {.x = a.x - b.x, .y = a.y - b.y};
}

// Return a vec3 consisting of the component-wise sum of the two arguments.
inline vec3 operator+(const vec3& a, const vec3& b) {
  return {.x = a.x + b.x, .y = a.y + b.y, .z = a.z + b.z};
}

// Return a vec3 consisting of the component-wise difference of the two args.
inline vec3 operator-(const vec3& a, const vec3& b) {
  return {.x = a.x - b.x, .y = a.y - b.y, .z = a.z - b.z};
}

// Return a vec4 consisting of the component-wise sum of the two arguments.
inline vec4 operator+(const vec4& a, const vec4& b) {
  return {.x = a.x + b.x, .y = a.y + b.y, .z = a.z + b.z, .w = a.w + b.w};
}

// Return a vec4 consisting of the component-wise difference of the two args.
inline vec4 operator-(const vec4& a, const vec4& b) {
  return {.x = a.x - b.x, .y = a.y - b.y, .z = a.z - b.z, .w = a.w - b.w};
}

}  // namespace gfx
}  // namespace ui
}  // namespace fuchsia

#endif  // LIB_UI_SCENIC_CPP_FIDL_MATH_H_
