// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_UTIL_UNWRAP_H_
#define GARNET_LIB_UI_SCENIC_UTIL_UNWRAP_H_

#include "lib/escher/geometry/bounding_box.h"
#include "lib/escher/geometry/transform.h"
#include "lib/escher/geometry/types.h"
#include "lib/ui/scenic/fidl/types.fidl.h"

namespace scene_manager {

inline escher::vec2 Unwrap(const scenic::vec2Ptr& args) {
  return {args->x, args->y};
}

inline escher::vec3 Unwrap(const scenic::vec3Ptr& args) {
  return {args->x, args->y, args->z};
}

inline escher::vec4 Unwrap(const scenic::vec4Ptr& args) {
  return {args->x, args->y, args->z, args->w};
}

inline escher::mat4 Unwrap(const scenic::mat4Ptr& args) {
  auto& m = args->matrix;
  return {m[0], m[1], m[2],  m[3],  m[4],  m[5],  m[6],  m[7],
          m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]};
}

inline escher::vec3 Unwrap(const scenic::ColorRgbPtr& args) {
  return {args->red, args->green, args->blue};
}

inline escher::vec4 Unwrap(const scenic::ColorRgbaPtr& args) {
  return {args->red, args->green, args->blue, args->alpha};
}

inline escher::quat Unwrap(const scenic::QuaternionPtr& args) {
  return {args->w, escher::vec3(args->x, args->y, args->z)};
}

inline escher::Transform Unwrap(const scenic::TransformPtr& args) {
  return {Unwrap(args->translation), Unwrap(args->scale),
          Unwrap(args->rotation), Unwrap(args->anchor)};
}

inline escher::BoundingBox Unwrap(const scenic::BoundingBoxPtr& args) {
  escher::vec3 min = Unwrap(args->min);
  escher::vec3 max = Unwrap(args->max);
  if (min.x > max.x || min.y > max.y || min.z > max.z) {
    // This bounding box is empty.
    return escher::BoundingBox();
  }
  return {min, max};
}

inline bool IsFloat(const scenic::ValuePtr& val) {
  // TODO: support variables of type kVector1.
  return val->which() == scenic::Value::Tag::VECTOR1;
}

inline bool IsVector2(const scenic::ValuePtr& val) {
  // TODO: support variables of type kVector2.
  return val->which() == scenic::Value::Tag::VECTOR2;
}

inline bool IsVector3(const scenic::ValuePtr& val) {
  // TODO: support variables of type kVector3.
  return val->which() == scenic::Value::Tag::VECTOR3;
}

inline bool IsVector4(const scenic::ValuePtr& val) {
  // TODO: support variables of type kVector4.
  return val->which() == scenic::Value::Tag::VECTOR4;
}

inline bool IsMatrix4x4(const scenic::ValuePtr& val) {
  // TODO: support variables of type kMatrix4x4.
  return val->which() == scenic::Value::Tag::MATRIX4X4;
}

inline bool IsQuaternion(const scenic::ValuePtr& val) {
  // TODO: support variables of type kQuaternion.
  return val->which() == scenic::Value::Tag::QUATERNION;
}

inline bool IsTransform(const scenic::ValuePtr& val) {
  // TODO: support variables of type kTransform.
  return val->which() == scenic::Value::Tag::TRANSFORM;
}

inline bool IsVariable(const scenic::ValuePtr& val) {
  return val->which() == scenic::Value::Tag::VARIABLE_ID;
}

template <typename ValuePtrT>
inline bool IsVariable(const ValuePtrT& val) {
  return val->variable_id != 0;
}

// Caller must verify that the value is a Matrix4x4 before calling this.
inline escher::mat4 UnwrapMatrix4x4(const scenic::ValuePtr& val) {
  FXL_DCHECK(IsMatrix4x4(val));
  return Unwrap(val->get_matrix4x4());
}

// Caller must verify that the value is a Transform before calling this.
inline escher::Transform UnwrapTransform(const scenic::ValuePtr& val) {
  FXL_DCHECK(IsTransform(val));
  return Unwrap(val->get_transform());
}

inline float UnwrapFloat(const scenic::FloatValuePtr& val) {
  FXL_DCHECK(!IsVariable(val)) << "variable values not yet implemented";
  return val->value;
}

inline escher::vec2 UnwrapVector2(const scenic::Vector2ValuePtr& val) {
  FXL_DCHECK(!IsVariable(val)) << "variable values not yet implemented";
  return Unwrap(val->value);
}

inline escher::vec3 UnwrapVector3(const scenic::Vector3ValuePtr& val) {
  FXL_DCHECK(!IsVariable(val)) << "variable values not yet implemented";
  return Unwrap(val->value);
}

inline escher::vec4 UnwrapVector4(const scenic::Vector4ValuePtr& val) {
  FXL_DCHECK(!IsVariable(val)) << "variable values not yet implemented";
  return Unwrap(val->value);
}

inline escher::quat UnwrapQuaternion(const scenic::QuaternionValuePtr& val) {
  FXL_DCHECK(!IsVariable(val)) << "variable values not yet implemented";
  return Unwrap(val->value);
}

inline bool Unwrap(const scenic::ValuePtr& value, float* out) {
  if (!IsVariable(value) && IsFloat(value)) {
    (*out) = value->get_vector1();
    return true;
  }
  return false;
}

inline bool Unwrap(const scenic::ValuePtr& value, escher::vec2* out) {
  if (!IsVariable(value) && IsVector2(value)) {
    (*out) = Unwrap(value->get_vector2());
    return true;
  }
  return false;
}

inline bool Unwrap(const scenic::ValuePtr& value, escher::vec3* out) {
  if (!IsVariable(value) && IsVector3(value)) {
    (*out) = Unwrap(value->get_vector3());
    return true;
  }
  return false;
}

inline bool Unwrap(const scenic::ValuePtr& value, escher::vec4* out) {
  if (!IsVariable(value) && IsVector4(value)) {
    (*out) = Unwrap(value->get_vector4());
    return true;
  }
  return false;
}

inline bool Unwrap(const scenic::ValuePtr& value, escher::quat* out) {
  if (!IsVariable(value) && IsQuaternion(value)) {
    (*out) = Unwrap(value->get_quaternion());
    return true;
  }
  return false;
}

inline bool Unwrap(const scenic::ValuePtr& value, escher::mat4* out) {
  if (!IsVariable(value) && IsMatrix4x4(value)) {
    (*out) = Unwrap(value->get_matrix4x4());
    return true;
  }
  return false;
}

inline bool Unwrap(const scenic::ValuePtr& value, escher::Transform* out) {
  if (!IsVariable(value) && IsTransform(value)) {
    (*out) = Unwrap(value->get_transform());
    return true;
  }
  return false;
}

}  // namespace scene_manager

#endif  // GARNET_LIB_UI_SCENIC_UTIL_UNWRAP_H_
