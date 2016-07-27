// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/gl/unique_buffer.h"
#include "ftl/memory/ref_counted.h"

namespace escher {

class Tessellation;

namespace gles2 {

// Immutable container for vertex indices and attribute data required to render
// a triangle mesh.
class Mesh : public ftl::RefCountedThreadSafe<Mesh>  {
 public:
  Mesh(const Tessellation& tessellation);

  const UniqueBuffer& vertices() const { return vertices_; }
  const UniqueBuffer& indices() const { return indices_; }
  const GLuint num_indices() const { return num_indices_; }

  GLuint stride() const { return stride_; }
  GLvoid* normal_offset() const {
    return reinterpret_cast<GLvoid*>(normal_offset_);
  }
  GLvoid* uv_offset() const {
    return reinterpret_cast<GLvoid*>(uv_offset_);
  }

  bool has_normals() const { return normal_offset_ > 0; }
  bool has_uv() const { return uv_offset_ > 0; }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(Mesh);
  ~Mesh();

  UniqueBuffer vertices_;
  UniqueBuffer indices_;
  GLuint num_indices_;
  GLuint stride_ = 0;
  GLuint normal_offset_ = 0;
  GLuint uv_offset_ = 0;
};

}  // namespace gles2
}  // namespace escher
