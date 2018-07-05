// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_SHAPE_MESH_BUILDER_H_
#define LIB_ESCHER_SHAPE_MESH_BUILDER_H_

#include "lib/escher/shape/mesh.h"
#include "lib/fxl/memory/ref_counted.h"

namespace escher {

// MeshBuilder is used by Escher clients to generate Meshes.  Clients should
// obtain one via Esher::NewMeshBuilder(), repeatedly call AddVertex() and
// AddIndex() to add data for the Mesh, and then call Build() once all data has
// been added.
class MeshBuilder : public fxl::RefCountedThreadSafe<MeshBuilder> {
 public:
  // Return a mesh constructed from the indices and vertices added by AddIndex()
  // and AddVertex(), respectively.  This can only be called once.
  virtual MeshPtr Build() = 0;

  // Copy the index into the staging buffer, so that it will be uploaded to the
  // GPU when Build() is called.
  MeshBuilder& AddIndex(uint32_t index);

  MeshBuilder& AddTriangle(uint32_t index0, uint32_t index1, uint32_t index2) {
    AddIndex(index0);
    AddIndex(index1);
    return AddIndex(index2);
  }

  // Copy |size| bytes of data to the staging buffer; this data represents a
  // single vertex.
  MeshBuilder& AddVertexData(const void* ptr, size_t size);

  // Wrap AddVertexData() to automatically obtain the size from the vertex.
  template <typename VertexT>
  MeshBuilder& AddVertex(const VertexT& v);

  // Return the size of a vertex for the given mesh-spec.
  size_t vertex_stride() const { return vertex_stride_; }

  // Return the number of indices that have been added to the builder, so far.
  size_t index_count() const { return index_count_; }

  // Return the number of vertices that have been added to the builder, so far.
  size_t vertex_count() const { return vertex_count_; }

  // Return pointer to start of data for the vertex at the specified index.
  uint8_t* GetVertex(size_t index) {
    FXL_DCHECK(index < vertex_count_);
    return vertex_staging_buffer_ + (index * vertex_stride_);
  }
  // Return pointer to the i-th index that was added.
  uint32_t* GetIndex(size_t i) {
    FXL_DCHECK(i < index_count_);
    return index_staging_buffer_ + i;
  }

 protected:
  MeshBuilder(size_t max_vertex_count, size_t max_index_count,
              size_t vertex_stride, uint8_t* vertex_staging_buffer,
              uint32_t* index_staging_buffer);
  FRIEND_REF_COUNTED_THREAD_SAFE(MeshBuilder);
  virtual ~MeshBuilder();

  const size_t max_vertex_count_;
  const size_t max_index_count_;
  const size_t vertex_stride_;
  uint8_t* vertex_staging_buffer_;
  uint32_t* index_staging_buffer_;
  size_t vertex_count_ = 0;
  size_t index_count_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(MeshBuilder);
};

typedef fxl::RefPtr<MeshBuilder> MeshBuilderPtr;

// Inline function definitions.

inline MeshBuilder& MeshBuilder::AddIndex(uint32_t index) {
  FXL_DCHECK(index_count_ < max_index_count_);
  index_staging_buffer_[index_count_++] = index;
  return *this;
}

inline MeshBuilder& MeshBuilder::AddVertexData(const void* ptr, size_t size) {
  FXL_DCHECK(vertex_count_ < max_vertex_count_);
  FXL_DCHECK(size <= vertex_stride_);
  size_t offset = vertex_stride_ * vertex_count_++;
  memcpy(vertex_staging_buffer_ + offset, ptr, size);
  return *this;
}

template <typename VertexT>
MeshBuilder& MeshBuilder::AddVertex(const VertexT& v) {
  AddVertexData(&v, sizeof(VertexT));
  return *this;
}

}  // namespace escher

#endif  // LIB_ESCHER_SHAPE_MESH_BUILDER_H_
