// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/composer/print_op.h"

namespace mozart {
namespace composer {

using mozart2::Op;
using mozart2::OpPtr;
using mozart2::Resource;

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
    case Resource::Tag::LINK:
      stream << "Link";
      break;
    case Resource::Tag::RECTANGLE:
      stream << "Rectangle";
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
    case Resource::Tag::LINK_NODE:
      stream << "LinkNode";
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

}  // namespace composer
}  // namespace mozart
