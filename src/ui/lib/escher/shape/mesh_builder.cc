// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/shape/mesh_builder.h"

#include "src/ui/lib/escher/escher.h"

namespace escher {

MeshBuilder::MeshBuilder(size_t max_vertex_count, size_t max_index_count, size_t vertex_stride)
    : max_vertex_count_(max_vertex_count),
      max_index_count_(max_index_count),
      vertex_stride_(vertex_stride),
      vertex_staging_buffer_(max_vertex_count * vertex_stride, 0u),
      index_staging_buffer_(max_index_count, 0u) {}

MeshBuilder::~MeshBuilder() {}

}  // namespace escher
