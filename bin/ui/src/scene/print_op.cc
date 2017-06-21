// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene/print_op.h"

namespace mozart {
namespace scene {

using mozart2::Op;
using mozart2::OpPtr;
using mozart2::Resource;
using mozart2::Value;

std::ostream& operator<<(std::ostream& stream, const mozart2::OpPtr& op) {
  switch (op->which()) {
    case Op::Tag::CREATE_RESOURCE:
      return stream << "[" << op->get_create_resource() << "]";
    default:
      return stream << "PRINTING NOT IMPLEMENTED FOR ALL OP TYPES";
  }
}

std::ostream& operator<<(std::ostream& stream,
                         const mozart2::CreateResourceOpPtr& op) {
  stream << "CreateResourceOp(id:" << op->id << " ";
  switch (op->resource->which()) {
    case Resource::Tag::MEMORY:
      stream << "Memory";
      break;
    case Resource::Tag::IMAGE:
      stream << "Image";
      break;
    case Resource::Tag::BUFFER:
      stream << "Buffer";
      break;
    case Resource::Tag::SCENE:
      stream << "Scene";
      break;
    case Resource::Tag::CAMERA:
      stream << "Camera";
      break;
    case Resource::Tag::DISPLAY_RENDERER:
      stream << "DisplayRenderer";
      break;
    case Resource::Tag::IMAGE_PIPE_RENDERER:
      stream << "ImagePipeRenderer";
      break;
    case Resource::Tag::DIRECTIONAL_LIGHT:
      stream << "DirectionalLight";
      break;
    case Resource::Tag::RECTANGLE:
      stream << "Rectangle";
      break;
    case Resource::Tag::ROUNDED_RECTANGLE:
      stream << "RoundedRectangle";
      break;
    case Resource::Tag::CIRCLE:
      stream << "Circle";
      break;
    case Resource::Tag::MESH:
      stream << "Mesh";
      break;
    case Resource::Tag::MATERIAL:
      stream << "Material";
      break;
    case Resource::Tag::CLIP_NODE:
      stream << "ClipNode";
      break;
    case Resource::Tag::ENTITY_NODE:
      stream << "EntityNode";
      break;
    case Resource::Tag::SHAPE_NODE:
      stream << "ShapeNode";
      break;
    case Resource::Tag::TAG_NODE:
      stream << "TagNode";
      break;
    case Resource::Tag::VARIABLE:
      stream << "Variable";
      break;
    case Resource::Tag::__UNKNOWN__:
      stream << "__UNKNOWN__";
      break;
  }
  return stream << ")";
}

std::ostream& operator<<(std::ostream& stream, const mozart2::Value::Tag& tag) {
  switch (tag) {
    case Value::Tag::VECTOR1:
      return stream << "vec1";
    case Value::Tag::VECTOR2:
      return stream << "vec2";
    case Value::Tag::VECTOR3:
      return stream << "vec3";
    case Value::Tag::VECTOR4:
      return stream << "vec4";
    case Value::Tag::MATRIX4X4:
      return stream << "mat4";
    case Value::Tag::COLOR_RGBA:
      return stream << "rgba";
    case Value::Tag::DEGREES:
      return stream << "degrees";
    case Value::Tag::QUATERNION:
      return stream << "quat";
    case Value::Tag::TRANSFORM:
      return stream << "transform";
    case Value::Tag::VARIABLE_ID:
      return stream << "variable";
    case Value::Tag::__UNKNOWN__:
      return stream << "__UNKNOWN__";
  }
}

}  // namespace scene
}  // namespace mozart
