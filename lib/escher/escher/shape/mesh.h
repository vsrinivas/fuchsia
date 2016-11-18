// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>

#include "escher/forward_declarations.h"
#include "escher/impl/resource.h"
#include "escher/shape/mesh_spec.h"

namespace escher {

// Immutable container for vertex indices and attribute data required to render
// a triangle mesh.
class Mesh : public impl::Resource {
 public:
  const MeshSpec spec;
  const uint32_t num_vertices;
  const uint32_t num_indices;

  // TODO: This is a temporary hack that shouldn't be necessary.
  virtual const impl::MeshSpecImpl& spec_impl() const = 0;

 protected:
  FRIEND_REF_COUNTED_THREAD_SAFE(Mesh);
  virtual ~Mesh();

 private:
  friend class escher::impl::MeshImpl;
  Mesh(impl::EscherImpl* escher,
       MeshSpec spec,
       uint32_t num_vertices,
       uint32_t num_indices);

  FTL_DISALLOW_COPY_AND_ASSIGN(Mesh);
};

typedef ftl::RefPtr<Mesh> MeshPtr;

}  // namespace escher
