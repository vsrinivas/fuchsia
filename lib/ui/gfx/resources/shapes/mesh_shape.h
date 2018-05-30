// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_SHAPES_MESH_SHAPE_H_
#define GARNET_LIB_UI_GFX_RESOURCES_SHAPES_MESH_SHAPE_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include "garnet/lib/ui/gfx/resources/buffer.h"
#include "garnet/lib/ui/gfx/resources/shapes/shape.h"

#include "lib/escher/shape/mesh.h"

namespace scenic {
namespace gfx {

// Encapsulates an Escher mesh.  The Scenic API allows clients to dynamically
// change the index/vertex buffers used by a MeshShape.
class MeshShape final : public Shape {
 public:
  static const ResourceTypeInfo kTypeInfo;

  MeshShape(Session* session, scenic::ResourceId id);

  // These correspond to BindMeshBuffersCommand in commands.fidl.
  bool BindBuffers(BufferPtr index_buffer,
                   ::fuchsia::ui::gfx::MeshIndexFormat index_format,
                   uint64_t index_offset, uint32_t index_count,
                   BufferPtr vertex_buffer,
                   const ::fuchsia::ui::gfx::MeshVertexFormat& vertex_format,
                   uint64_t vertex_offset, uint32_t vertex_count,
                   escher::BoundingBox bounding_box);

  // |Resource|.
  void Accept(class ResourceVisitor* visitor) override;

  // |Shape|.
  bool GetIntersection(const escher::ray4& ray,
                       float* out_distance) const override;

  // |Shape|.
  escher::Object GenerateRenderObject(
      const escher::mat4& transform,
      const escher::MaterialPtr& material) override;

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
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_RESOURCES_SHAPES_MESH_SHAPE_H_
