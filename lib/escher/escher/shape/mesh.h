// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "ftl/memory/ref_counted.h"

namespace escher {

struct Tessellation;

// Immutable container for vertex indices and attribute data required to render
// a triangle mesh.
class Mesh : public ftl::RefCountedThreadSafe<Mesh> {
 public:
  Mesh(const Tessellation& tessellation);

  uint32_t num_indices() const { return num_indices_; }

  uint32_t stride() const { return stride_; }
  uint32_t normal_offset() const { return normal_offset_; }
  uint32_t uv_offset() const { return uv_offset_; }

  bool has_normals() const { return normal_offset_ > 0; }
  bool has_uv() const { return uv_offset_ > 0; }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(Mesh);
  ~Mesh();

  uint32_t num_indices_;
  uint32_t stride_ = 0;
  uint32_t normal_offset_ = 0;
  uint32_t uv_offset_ = 0;
};

}  // namespace escher
