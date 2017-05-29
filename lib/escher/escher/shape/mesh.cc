// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/shape/mesh.h"

#include "escher/impl/escher_impl.h"
#include "escher/resources/resource_life_preserver.h"

namespace escher {

const ResourceTypeInfo Mesh::kTypeInfo("Mesh",
                                       ResourceType::kResource,
                                       ResourceType::kWaitableResource,
                                       ResourceType::kMesh);

Mesh::Mesh(ResourceLifePreserver* life_preserver,
           MeshSpec spec,
           uint32_t num_vertices,
           uint32_t num_indices)
    : WaitableResource(life_preserver),
      spec(std::move(spec)),
      num_vertices(num_vertices),
      num_indices(num_indices) {}

Mesh::~Mesh() {}

}  // namespace escher
