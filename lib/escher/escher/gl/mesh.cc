// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/gl/mesh.h"

#include <alloca.h>

#include "escher/geometry/tessellation.h"

#include "ftl/logging.h"

namespace escher {

Mesh::Mesh(const Tessellation& tessellation)
    : indices_(MakeUniqueIndexBuffer(tessellation.indices)),
      num_indices_(static_cast<GLuint>(tessellation.indices.size())) {
  tessellation.SanityCheck();

  stride_ = sizeof(Tessellation::position_type);
  if (!tessellation.normals.empty()) {
    normal_offset_ = stride_;
    stride_ += sizeof(Tessellation::normal_type);
  }
  if (!tessellation.uvs.empty()) {
    uv_offset_ = stride_;
    stride_ += sizeof(Tessellation::uv_type);
  }

  // Interleave positions/normals/tex-coords before storing them in a buffer.
  size_t num_bytes = tessellation.positions.size() * stride_;
  GLubyte* bytes = static_cast<GLubyte*>(alloca(num_bytes));
  FTL_DCHECK(bytes);
  const auto& positions = tessellation.positions;
  const auto& normals = tessellation.normals;
  const auto& uvs = tessellation.uvs;
  for (int i = 0; i < positions.size(); ++i) {
    // Address of current vertex.
    GLubyte* ptr = bytes + i * stride_;
    // Store vertex position.
    auto position = reinterpret_cast<Tessellation::position_type*>(ptr);
    *position = positions[i];
    // Store vertex normal, if any.
    if (normal_offset_ > 0) {
      auto normal = reinterpret_cast<Tessellation::normal_type*>(
          ptr + normal_offset_);
      *normal = normals[i];
    }
    // Store vertex texture coordinates, if any.
    if (uv_offset_ > 0) {
      auto uv = reinterpret_cast<Tessellation::uv_type*>(
          ptr + uv_offset_);
      *uv = uvs[i];
    }
  }

  // Finally, upload the interleaved vertex data to a VBO.
  vertices_ = MakeUniqueVertexBuffer(bytes, num_bytes);

  FTL_DCHECK(glGetError() == GL_NO_ERROR);
}

Mesh::~Mesh() {}

}  // namespace escher
