// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/shapes/mesh_shape.h"

#include "src/ui/lib/escher/geometry/intersection.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo MeshShape::kTypeInfo = {ResourceType::kShape | ResourceType::kMesh,
                                               "MeshShape"};

MeshShape::MeshShape(Session* session, SessionId session_id, ResourceId id)
    : Shape(session, session_id, id, MeshShape::kTypeInfo) {}

bool MeshShape::GetIntersection(const escher::ray4& ray, float* out_distance) const {
  FX_DCHECK(out_distance);
  // TODO(fxbug.dev/23518): implement mesh-ray intersection.
  escher::Interval interval;
  bool hit = IntersectRayBox(ray, bounding_box_, &interval);
  if (hit) {
    *out_distance = interval.min();
  }
  return hit;
}

bool MeshShape::BindBuffers(BufferPtr index_buffer,
                            ::fuchsia::ui::gfx::MeshIndexFormat index_format, uint64_t index_offset,
                            uint32_t index_count, BufferPtr vertex_buffer,
                            const ::fuchsia::ui::gfx::MeshVertexFormat& vertex_format,
                            uint64_t vertex_offset, uint32_t vertex_count,
                            escher::BoundingBox bounding_box, ErrorReporter* error_reporter) {
  if (index_format != ::fuchsia::ui::gfx::MeshIndexFormat::kUint32) {
    // TODO(fxbug.dev/23519): only 32-bit indices are supported.
    error_reporter->ERROR()
        << "BindBuffers::BindBuffers(): TODO(fxbug.dev/23519): only 32-bit indices are supported.";
    return false;
  }
  escher::MeshSpec spec;
  switch (vertex_format.position_type) {
    case ::fuchsia::ui::gfx::ValueType::kVector2:
      spec.attributes[0] |= escher::MeshAttribute::kPosition2D;
      break;
    case ::fuchsia::ui::gfx::ValueType::kVector3:
      spec.attributes[0] |= escher::MeshAttribute::kPosition3D;
      break;
    default:
      error_reporter->ERROR() << "MeshShape::BindBuffers(): bad vertex position format.";
      return false;
  }
  switch (vertex_format.normal_type) {
    case ::fuchsia::ui::gfx::ValueType::kNone:
      break;
    default:
      error_reporter->ERROR() << "MeshShape::BindBuffers(): bad vertex normal format.";
      return false;
  }
  switch (vertex_format.tex_coord_type) {
    case ::fuchsia::ui::gfx::ValueType::kVector2:
      spec.attributes[0] |= escher::MeshAttribute::kUV;
      break;
    case ::fuchsia::ui::gfx::ValueType::kNone:
      break;
    default:
      error_reporter->ERROR() << "MeshShape::BindBuffers(): bad vertex tex-coord format.";
      return false;
  }
  mesh_ = fxl::MakeRefCounted<escher::Mesh>(
      resource_context().escher_resource_recycler, spec, bounding_box, vertex_count, index_count,
      vertex_buffer->escher_buffer(), index_buffer->escher_buffer(), vertex_offset, index_offset);

  bounding_box_ = bounding_box;
  index_buffer_ = std::move(index_buffer);
  vertex_buffer_ = std::move(vertex_buffer);

  return true;
}

}  // namespace gfx
}  // namespace scenic_impl
