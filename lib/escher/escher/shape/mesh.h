// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>

// TODO: Consider defining MeshSpec without using Vulkan types.
#include <vulkan/vulkan.hpp>

#include "escher/forward_declarations.h"
#include "ftl/memory/ref_counted.h"

namespace escher {

struct MeshSpec {
  vk::VertexInputBindingDescription binding;
  std::vector<vk::VertexInputAttributeDescription> attributes;
  std::vector<std::string> attribute_names;

  uint32_t GetVertexStride() const { return binding.stride; }
  uint32_t GetVertexBinding() const { return binding.binding; }
};

// Immutable container for vertex indices and attribute data required to render
// a triangle mesh.
class Mesh : public ftl::RefCountedThreadSafe<Mesh> {
 public:
  const MeshSpec spec;
  const uint32_t num_vertices;
  const uint32_t num_indices;

 private:
  friend class escher::impl::MeshImpl;
  Mesh(MeshSpec spec, uint32_t num_vertices, uint32_t num_indices);
  virtual ~Mesh() {}

  FRIEND_REF_COUNTED_THREAD_SAFE(Mesh);
};

typedef ftl::RefPtr<Mesh> MeshPtr;

}  // namespace escher
