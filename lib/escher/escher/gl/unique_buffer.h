// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <glm/glm.hpp>
#include <vector>

#include "escher/gl/unique_object.h"

namespace escher {
namespace internal {

inline void DeleteBuffer(GLuint id) {
  glDeleteBuffers(1, &id);
}

}  // internal

typedef UniqueObject<internal::DeleteBuffer> UniqueBuffer;

// Create a new, empty buffer.
UniqueBuffer MakeUniqueBuffer();

// Create a new "STATIC_DRAW" vertex buffer with the specified contents.
UniqueBuffer MakeUniqueVertexBuffer(const GLubyte* bytes, size_t num_bytes);
// Generic version of the above.
template <typename VertexT>
UniqueBuffer MakeUniqueVertexBuffer(const std::vector<VertexT>& vertices) {
  const GLubyte* ptr = reinterpret_cast<const GLubyte*>(&vertices[0]);
  return MakeUniqueVertexBuffer(ptr, sizeof(VertexT) * vertices.size());
}

// Create a new "STATIC_DRAW" index buffer with the specified contents.
UniqueBuffer MakeUniqueIndexBuffer(const std::vector<GLushort>& indices);

}  // namespace escher
