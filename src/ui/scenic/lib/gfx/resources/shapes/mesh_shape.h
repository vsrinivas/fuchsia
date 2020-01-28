// Copyright 2017 The Fuchsia Authors. All rights reserved.

// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_SHAPES_MESH_SHAPE_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_SHAPES_MESH_SHAPE_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>

#include "src/ui/lib/escher/shape/mesh.h"
#include "src/ui/scenic/lib/gfx/resources/buffer.h"
#include "src/ui/scenic/lib/gfx/resources/shapes/shape.h"

namespace scenic_impl {
namespace gfx {

// Encapsulates an Escher mesh.  The Scenic API allows clients to dynamically
// change the index/vertex buffers used by a MeshShape.
class MeshShape final : public Shape {
 public:
  static const ResourceTypeInfo kTypeInfo;

  MeshShape(Session* session, SessionId session_id, ResourceId id);

  // These correspond to BindMeshBuffersCmd in commands.fidl.
  bool BindBuffers(BufferPtr index_buffer, ::fuchsia::ui::gfx::MeshIndexFormat index_format,
                   uint64_t index_offset, uint32_t index_count, BufferPtr vertex_buffer,
                   const ::fuchsia::ui::gfx::MeshVertexFormat& vertex_format,
                   uint64_t vertex_offset, uint32_t vertex_count, escher::BoundingBox bounding_box,
                   ErrorReporter* error_reporter);

  // |Resource|.
  void Accept(class ResourceVisitor* visitor) override;

  // |Shape|.
  bool GetIntersection(const escher::ray4& ray, float* out_distance) const override;

  const escher::MeshPtr& escher_mesh() const { return mesh_; }
  const BufferPtr& index_buffer() const { return index_buffer_; }
  const BufferPtr& vertex_buffer() const { return vertex_buffer_; }

 private:
  escher::MeshPtr mesh_;
  escher::BoundingBox bounding_box_;
  BufferPtr index_buffer_;
  BufferPtr vertex_buffer_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_SHAPES_MESH_SHAPE_H_
