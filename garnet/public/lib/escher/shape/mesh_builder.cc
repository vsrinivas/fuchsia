// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/shape/mesh_builder.h"

#include "lib/escher/escher.h"

namespace escher {

MeshBuilder::MeshBuilder(size_t max_vertex_count, size_t max_index_count,
                         size_t vertex_stride, uint8_t* vertex_staging_buffer,
                         uint32_t* index_staging_buffer)
    : max_vertex_count_(max_vertex_count),
      max_index_count_(max_index_count),
      vertex_stride_(vertex_stride),
      vertex_staging_buffer_(vertex_staging_buffer),
      index_staging_buffer_(index_staging_buffer) {}

MeshBuilder::~MeshBuilder() {}

}  // namespace escher
