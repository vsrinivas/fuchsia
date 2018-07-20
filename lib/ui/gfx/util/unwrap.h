// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_UTIL_UNWRAP_H_
#define GARNET_LIB_UI_GFX_UTIL_UNWRAP_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include "lib/escher/geometry/bounding_box.h"
#include "lib/escher/geometry/transform.h"
#include "lib/escher/geometry/types.h"
#include "lib/fxl/logging.h"

namespace scenic {
namespace gfx {

inline escher::vec2 Unwrap(::fuchsia::ui::gfx::vec2 args) {
  return {args.x, args.y};
}

inline escher::vec3 Unwrap(::fuchsia::ui::gfx::vec3 args) {
  return {args.x, args.y, args.z};
}

inline escher::vec4 Unwrap(::fuchsia::ui::gfx::vec4 args) {
  return {args.x, args.y, args.z, args.w};
}

inline escher::mat4 Unwrap(::fuchsia::ui::gfx::mat4 args) {
  auto& m = args.matrix;
  return {m[0], m[1], m[2],  m[3],  m[4],  m[5],  m[6],  m[7],
          m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]};
}

inline escher::vec3 Unwrap(::fuchsia::ui::gfx::ColorRgb args) {
  return {args.red, args.green, args.blue};
}

inline escher::vec4 Unwrap(::fuchsia::ui::gfx::ColorRgba args) {
  return {args.red, args.green, args.blue, args.alpha};
}

inline escher::quat Unwrap(::fuchsia::ui::gfx::Quaternion args) {
  return {args.w, escher::vec3(args.x, args.y, args.z)};
}

inline escher::Transform Unwrap(::fuchsia::ui::gfx::FactoredTransform args) {
  return {Unwrap(args.translation), Unwrap(args.scale), Unwrap(args.rotation),
          Unwrap(args.anchor)};
}

inline escher::BoundingBox Unwrap(::fuchsia::ui::gfx::BoundingBox args) {
  escher::vec3 min = Unwrap(args.min);
  escher::vec3 max = Unwrap(args.max);
  if (min.x > max.x || min.y > max.y || min.z > max.z) {
    // This bounding box is empty.
    return escher::BoundingBox();
  }
  return {min, max};
}

inline bool IsFloat(const ::fuchsia::ui::gfx::Value& val) {
  // TODO: support variables of type kVector1.
  return val.Which() == ::fuchsia::ui::gfx::Value::Tag::kVector1;
}

inline bool IsVector2(const ::fuchsia::ui::gfx::Value& val) {
  // TODO: support variables of type kVector2.
  return val.Which() == ::fuchsia::ui::gfx::Value::Tag::kVector2;
}

inline bool IsVector3(const ::fuchsia::ui::gfx::Value& val) {
  // TODO: support variables of type kVector3.
  return val.Which() == ::fuchsia::ui::gfx::Value::Tag::kVector3;
}

inline bool IsVector4(const ::fuchsia::ui::gfx::Value& val) {
  // TODO: support variables of type kVector4.
  return val.Which() == ::fuchsia::ui::gfx::Value::Tag::kVector4;
}

inline bool IsMatrix4x4(const ::fuchsia::ui::gfx::Value& val) {
  // TODO: support variables of type kMatrix4x4.
  return val.Which() == ::fuchsia::ui::gfx::Value::Tag::kMatrix4x4;
}

inline bool IsQuaternion(const ::fuchsia::ui::gfx::Value& val) {
  // TODO: support variables of type kQuaternion.
  return val.Which() == ::fuchsia::ui::gfx::Value::Tag::kQuaternion;
}

inline bool IsTransform(const ::fuchsia::ui::gfx::Value& val) {
  // TODO: support variables of type kTransform.
  return val.Which() == ::fuchsia::ui::gfx::Value::Tag::kTransform;
}

inline bool IsVariable(const ::fuchsia::ui::gfx::Value& val) {
  return val.Which() == ::fuchsia::ui::gfx::Value::Tag::kVariableId;
}

template <typename ValueT>
inline bool IsVariable(const ValueT& val) {
  return val.variable_id != 0;
}

// Caller must verify that the value is a Matrix4x4 before calling this.
inline escher::mat4 UnwrapMatrix4x4(const ::fuchsia::ui::gfx::Value& val) {
  FXL_DCHECK(IsMatrix4x4(val));
  return Unwrap(val.matrix4x4());
}

// Caller must verify that the value is a Transform before calling this.
inline escher::Transform UnwrapTransform(const ::fuchsia::ui::gfx::Value& val) {
  FXL_DCHECK(IsTransform(val));
  return Unwrap(val.transform());
}

inline float UnwrapFloat(const ::fuchsia::ui::gfx::FloatValue& val) {
  FXL_DCHECK(!IsVariable(val)) << "variable values not yet implemented";
  return val.value;
}

inline escher::vec2 UnwrapVector2(const ::fuchsia::ui::gfx::Vector2Value& val) {
  FXL_DCHECK(!IsVariable(val)) << "variable values not yet implemented";
  return Unwrap(val.value);
}

inline escher::vec3 UnwrapVector3(const ::fuchsia::ui::gfx::Vector3Value& val) {
  FXL_DCHECK(!IsVariable(val)) << "variable values not yet implemented";
  return Unwrap(val.value);
}

inline escher::vec4 UnwrapVector4(const ::fuchsia::ui::gfx::Vector4Value& val) {
  FXL_DCHECK(!IsVariable(val)) << "variable values not yet implemented";
  return Unwrap(val.value);
}

inline escher::quat UnwrapQuaternion(
    const ::fuchsia::ui::gfx::QuaternionValue& val) {
  FXL_DCHECK(!IsVariable(val)) << "variable values not yet implemented";
  return Unwrap(val.value);
}

inline bool Unwrap(const ::fuchsia::ui::gfx::Value& value, float* out) {
  if (!IsVariable(value) && IsFloat(value)) {
    (*out) = value.vector1();
    return true;
  }
  return false;
}

inline bool Unwrap(const ::fuchsia::ui::gfx::Value& value, escher::vec2* out) {
  if (!IsVariable(value) && IsVector2(value)) {
    (*out) = Unwrap(value.vector2());
    return true;
  }
  return false;
}

inline bool Unwrap(const ::fuchsia::ui::gfx::Value& value, escher::vec3* out) {
  if (!IsVariable(value) && IsVector3(value)) {
    (*out) = Unwrap(value.vector3());
    return true;
  }
  return false;
}

inline bool Unwrap(const ::fuchsia::ui::gfx::Value& value, escher::vec4* out) {
  if (!IsVariable(value) && IsVector4(value)) {
    (*out) = Unwrap(value.vector4());
    return true;
  }
  return false;
}

inline bool Unwrap(const ::fuchsia::ui::gfx::Value& value, escher::quat* out) {
  if (!IsVariable(value) && IsQuaternion(value)) {
    (*out) = Unwrap(value.quaternion());
    return true;
  }
  return false;
}

inline bool Unwrap(const ::fuchsia::ui::gfx::Value& value, escher::mat4* out) {
  if (!IsVariable(value) && IsMatrix4x4(value)) {
    (*out) = Unwrap(value.matrix4x4());
    return true;
  }
  return false;
}

inline bool Unwrap(const ::fuchsia::ui::gfx::Value& value,
                   escher::Transform* out) {
  if (!IsVariable(value) && IsTransform(value)) {
    (*out) = Unwrap(value.transform());
    return true;
  }
  return false;
}

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_UTIL_UNWRAP_H_
