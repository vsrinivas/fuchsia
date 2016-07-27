// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/gl/gles2/unique_buffer.h"

namespace escher {
namespace gles2 {

UniqueBuffer MakeUniqueBuffer() {
  GLuint id = 0;
  glGenBuffers(1, &id);
  UniqueBuffer buffer;
  buffer.Reset(id);
  return buffer;
}

UniqueBuffer MakeUniqueVertexBuffer(const GLubyte* bytes, size_t num_bytes) {
  UniqueBuffer buffer(MakeUniqueBuffer());
  glBindBuffer(GL_ARRAY_BUFFER, buffer.id());
  glBufferData(GL_ARRAY_BUFFER, num_bytes, bytes, GL_STATIC_DRAW);
  return buffer;
}

UniqueBuffer MakeUniqueIndexBuffer(const std::vector<GLushort>& indices) {
  UniqueBuffer buffer(MakeUniqueBuffer());
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer.id());
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               sizeof(GLushort) * indices.size(),
               &indices[0],
               GL_STATIC_DRAW);
  return buffer;
}

}  // namespace gles2
}  // namespace escher
